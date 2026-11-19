#include "pch.h"
#include "UIRenderer.h"
#include "Camera/Camera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/UI/UIBindings.h"
#include "UI/UIElement.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace CoreEngine
{
    namespace
    {
        /// @brief UI を重ねている間の、切る四角の状態
        struct OverlayClipState
        {
            uint32_t   targetWidth = 0;
            uint32_t   targetHeight = 0;
            bool       applied = false;
            D3D12_RECT current{};
        };

        OverlayClipState& ClipState()
        {
            static OverlayClipState state;
            return state;
        }
    }

    void UIRenderer::Initialize(ID3D12Device* device) {
        const std::string debugName = GetPipelineDebugName();

        // SpriteRenderer と同じシンプル構成
        RootSignatureConfig config;

        // UI はテキスト等で滑らかさが欲しいので Linear サンプラー
        config.ConfigureSampler("gSampler", GetSamplerConfig());

        const std::string reflectionName = debugName + "Renderer";
        const auto shaders = BuildShadersAndRootSignature(device,
            GetVertexShaderPath(), GetPixelShaderPath(), reflectionName, config, debugName);
        const std::span<const ShaderBindingDecl> decls = GetBindingDecls();
        bindings_ = BindingTable::Resolve(*reflectionData_, decls.data(), decls.size(), reflectionName);

        bool result = psoMg_->CreateBuilder()
            .SetDebugName(debugName)
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, shaders.vs.Get(), shaders.ps.Get(), rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create " + debugName + " Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
    }

    std::span<const ShaderBindingDecl> UIRenderer::GetBindingDecls() const {
        return UIBind::kDecls;
    }

    void UIRenderer::Initialize(GraphicsCore* dxCommon) {
        dxCommon_ = dxCommon;

        Initialize(dxCommon->GetDevice());

        quadIndexResource_ = ResourceFactory::CreateQuadIndexBuffer(dxCommon->GetDevice(), quadIndexBufferView_,
            kMaxQuadsPerImage);

        // フレームごとに変換行列の定数バッファを 1 本作り、マップしたままにする
        const size_t poolCount = GetConstantBufferPoolCount();
        if (poolCount == 0) {
            return;
        }
        for (UINT frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
            transformBuffers_[frameIndex] = ResourceFactory::CreateBufferResource(
                dxCommon_->GetDevice(), kTransformStride * poolCount);
            transformBuffers_[frameIndex]->Map(0, nullptr, reinterpret_cast<void**>(&transformMapped_[frameIndex]));
        }
    }

    void UIRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        // per-frame リソースの添字は FrameSync のスロット番号を使う
        // （スワップチェーンのインデックスは ResizeBuffers で 0 に戻るため使わない）
        UINT frameIndex = dxCommon_->Frame().FrameIndex();

        // フレーム切り替え時のみバッファインデックスをリセット
        if (frameIndex != currentFrameIndex_) {
            currentFrameIndex_ = frameIndex;
            currentBufferIndex_ = 0;
        }

        BeginBlendModePass(cmdList, blendMode);
        boundPipeline_ = nullptr;
    }

    const ShaderMaterialPipeline* UIRenderer::GetMaterialPipeline(const ShaderMaterial& material) {
        if (!dxCommon_) {
            return nullptr;
        }
        return materialPipelines_.Get(*dxCommon_, *shaderCompiler_, *reflectionBuilder_, material);
    }

    void UIRenderer::BindPipeline(ID3D12GraphicsCommandList* cmdList, const ShaderMaterialPipeline* pipeline) {
        if (pipeline == boundPipeline_) {
            return;
        }
        if (pipeline) {
            cmdList->SetGraphicsRootSignature(pipeline->GetRootSignature());
            cmdList->SetPipelineState(pipeline->GetPipelineState(currentBlendMode_));
            if (sceneDepth_) {
                if (pipeline->GetSceneDepthSlot().IsValid() && sceneDepth_->GetFarTextureSRV().ptr != 0) {
                    cmdList->SetGraphicsRootDescriptorTable(pipeline->GetSceneDepthSlot().index,
                        sceneDepth_->GetFarTextureSRV());
                }
                if (pipeline->GetSceneDepthParamsSlot().IsValid() && sceneDepth_->GetShaderParams() != 0) {
                    cmdList->SetGraphicsRootConstantBufferView(pipeline->GetSceneDepthParamsSlot().index,
                        sceneDepth_->GetShaderParams());
                }
            }
        } else {
            cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
            cmdList->SetPipelineState(pipelineState_);
        }
        boundPipeline_ = pipeline;
    }

    void UIRenderer::EndPass() {
        // 何もしない
    }

    void UIRenderer::SetCamera(const Camera* camera) {
        // UI はカメラ非依存（スクリーン固定座標）
        (void)camera;
    }

    size_t UIRenderer::GetAvailableConstantBuffer() {
        if (currentBufferIndex_ >= kMaxUICount) {
            // 上限を超えた要素は最後の枠を共有するので、位置と大きさが崩れる
            if (!overflowWarned_) {
                overflowWarned_ = true;
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "UI 要素が 1 フレームの上限 {} 個を超えました。超えた分は正しい位置に描かれません", kMaxUICount);
            }
            return kMaxUICount - 1;
        }
        size_t bufferIndex = currentBufferIndex_;
        currentBufferIndex_++;
        return bufferIndex;
    }

    void UIRenderer::SetReferenceResolution(float width, float height) {
        referenceWidth_ = width;
        referenceHeight_ = height;
    }

    Vector2 UIRenderer::GetScreenSize() const {
        // 基準解像度が指定されていればそれを使用、未指定ならエンジン既定の基準解像度。
        // ウィンドウサイズへ追従させてはいけない：クライアント領域の縦横比が変わる
        // （フルスクリーン解除など）たびに UI の配置・比率が崩れる
        float w = (referenceWidth_  > 0.0f) ? referenceWidth_  : static_cast<float>(WinApp::kReferenceWidth);
        float h = (referenceHeight_ > 0.0f) ? referenceHeight_ : static_cast<float>(WinApp::kReferenceHeight);
        return { w, h };
    }

    Matrix4x4 UIRenderer::CalculateProjectionMatrix() const {
        // 左上原点 (0,0) → 右下 (screenW, screenH) のスクリーン座標系
        Vector2 screen = GetScreenSize();
        return MathCore::Rendering::Orthographic(
            0.0f, 0.0f,
            screen.x, screen.y,
            0.0f, 100.0f);
    }

    Matrix4x4 UIRenderer::CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const {
        // ローカル変換
        Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, position);

        // ビューは単位行列（UI はカメラ非依存）
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();

        return worldMatrix * viewMatrix * CalculateProjectionMatrix();
    }

    Matrix4x4 UIRenderer::CalculateWVPFromWorld(const Matrix4x4& world) const {
        // ビューは単位行列（UI はカメラ非依存）
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();

        return world * viewMatrix * CalculateProjectionMatrix();
    }

    void UIRenderer::BeginOverlay(uint32_t width, uint32_t height) {
        OverlayClipState& state = ClipState();
        state.targetWidth = width;
        state.targetHeight = height;
        state.applied = false;
    }

    void UIRenderer::EndOverlay(ID3D12GraphicsCommandList* cmdList) {
        OverlayClipState& state = ClipState();
        if (state.applied && cmdList) {
            const D3D12_RECT full = { 0, 0, static_cast<LONG>(state.targetWidth), static_cast<LONG>(state.targetHeight) };
            cmdList->RSSetScissorRects(1, &full);
        }
        state.applied = false;
        state.targetWidth = 0;
        state.targetHeight = 0;
    }

    void UIRenderer::ApplyClip(ID3D12GraphicsCommandList* cmdList, const UIRect* clip) const {
        OverlayClipState& state = ClipState();
        if (!cmdList || state.targetWidth == 0 || state.targetHeight == 0) {
            return;
        }
        if (!clip) {
            if (state.applied) {
                const D3D12_RECT full = { 0, 0, static_cast<LONG>(state.targetWidth), static_cast<LONG>(state.targetHeight) };
                cmdList->RSSetScissorRects(1, &full);
                state.applied = false;
            }
            return;
        }

        // キャンバス座標を描く先の px へ直し、外側へ丸める
        const Vector2 canvas = GetScreenSize();
        const float scaleX = static_cast<float>(state.targetWidth) / canvas.x;
        const float scaleY = static_cast<float>(state.targetHeight) / canvas.y;
        const auto clampX = [&](float value) {
            return static_cast<LONG>(std::clamp(value, 0.0f, static_cast<float>(state.targetWidth)));
        };
        const auto clampY = [&](float value) {
            return static_cast<LONG>(std::clamp(value, 0.0f, static_cast<float>(state.targetHeight)));
        };
        D3D12_RECT rect{};
        rect.left = clampX(std::floor(clip->min.x * scaleX));
        rect.top = clampY(std::floor(clip->min.y * scaleY));
        rect.right = (std::max)(rect.left, clampX(std::ceil(clip->max.x * scaleX)));
        rect.bottom = (std::max)(rect.top, clampY(std::ceil(clip->max.y * scaleY)));

        if (state.applied && rect.left == state.current.left && rect.top == state.current.top
            && rect.right == state.current.right && rect.bottom == state.current.bottom) {
            return;
        }
        cmdList->RSSetScissorRects(1, &rect);
        state.current = rect;
        state.applied = true;
    }
}

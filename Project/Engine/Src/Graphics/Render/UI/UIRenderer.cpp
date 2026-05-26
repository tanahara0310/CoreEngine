#include "pch.h"
#include "UIRenderer.h"
#include "Camera/ICamera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "WinApp/WinApp.h"
#include <cassert>

namespace CoreEngine
{
    void UIRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/UI/UI.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/UI/UI.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "UIRenderer");

        // SpriteRenderer と同じシンプル構成
        RootSignatureConfig config = RootSignatureConfig::Simple();

        // UI はテキスト等で滑らかさが欲しいので Linear サンプラー
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());

        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Failed to create UI Root Signature: " + buildResult.errorMessage);
        }

        bool result = psoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create UI Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
    }

    int UIRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void UIRenderer::Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory) {
        dxCommon_ = dxCommon;
        resourceFactory_ = resourceFactory;

        Initialize(dxCommon->GetDevice());

        // フレームごとの定数バッファプール
        for (UINT frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
            auto& matResources = materialResources_[frameIndex];
            auto& tfResources = transformResources_[frameIndex];
            auto& matData = materialDataPool_[frameIndex];
            auto& tfData = transformDataPool_[frameIndex];

            matResources.resize(kMaxUICount);
            tfResources.resize(kMaxUICount);
            matData.resize(kMaxUICount);
            tfData.resize(kMaxUICount);

            for (size_t i = 0; i < kMaxUICount; ++i) {
                // マテリアル定数バッファ（永続マッピング）
                matResources[i] = resourceFactory_->CreateBufferResource(dxCommon_->GetDevice(), sizeof(UIMaterial));
                matResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&matData[i]));

                // トランスフォーム定数バッファ（永続マッピング）
                tfResources[i] = resourceFactory_->CreateBufferResource(dxCommon_->GetDevice(), sizeof(TransformationMatrix));
                tfResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&tfData[i]));
            }
        }
    }

    void UIRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        UINT frameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

        // フレーム切り替え時のみバッファインデックスをリセット
        if (frameIndex != currentFrameIndex_) {
            currentFrameIndex_ = frameIndex;
            currentBufferIndex_ = 0;
        }

        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            pipelineState_ = psoMg_->GetPipelineState(blendMode);
        }

        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(pipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void UIRenderer::EndPass() {
        // 何もしない
    }

    void UIRenderer::SetCamera(const ICamera* camera) {
        // UI はカメラ非依存（スクリーン固定座標）
        (void)camera;
    }

    size_t UIRenderer::GetAvailableConstantBuffer() {
        if (currentBufferIndex_ >= kMaxUICount) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: UI count exceeded maximum! Increase kMaxUICount or reduce UI count.\n");
#endif
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
        // 基準解像度が指定されていればそれを使用、未指定ならウィンドウサイズに追従
        float w = (referenceWidth_  > 0.0f) ? referenceWidth_  : static_cast<float>(WinApp::GetCurrentClientWidthStatic());
        float h = (referenceHeight_ > 0.0f) ? referenceHeight_ : static_cast<float>(WinApp::GetCurrentClientHeightStatic());
        return { w, h };
    }

    Matrix4x4 UIRenderer::CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const {
        // ローカル変換
        Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, position);

        // ビューは単位行列（UI はカメラ非依存）
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();

        // 投影：左上原点 (0,0) → 右下 (screenW, screenH) のスクリーン座標系
        Vector2 screen = GetScreenSize();
        Matrix4x4 projectionMatrix = MathCore::Rendering::Orthographic(
            0.0f, 0.0f,
            screen.x, screen.y,
            0.0f, 100.0f);

        return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
    }
}

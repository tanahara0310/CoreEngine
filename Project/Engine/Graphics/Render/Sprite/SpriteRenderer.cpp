#include "SpriteRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "SpriteMaterial.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"
#include "Engine/Graphics/RootSignature/RootSignatureConfig.h"
#include "WinApp/WinApp.h"
#include <cassert>


namespace CoreEngine
{
    void SpriteRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Sprite/Sprite.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Sprite/Sprite.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "SpriteRenderer");
        
        // シンプルな設定を使用
        RootSignatureConfig config = RootSignatureConfig::Simple();
        
        // スプライト用サンプラー設定（ポイントフィルタリング＋クランプ）
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());
        
        // RootSignatureを構築
        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
        
        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Sprite Root Signature: " + buildResult.errorMessage);
        }

        bool result = psoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create Sprite Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
    }

    int SpriteRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void SpriteRenderer::Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory) {
        dxCommon_ = dxCommon;
        resourceFactory_ = resourceFactory;

        // デバイスを使って基本初期化
        Initialize(dxCommon->GetDevice());

        // フレームごとに定数バッファプールを作成（ダブルバッファリング対応）
        for (UINT frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
            auto& matResources = materialResources_[frameIndex];
            auto& tfResources = transformResources_[frameIndex];
            auto& matData = materialDataPool_[frameIndex];
            auto& tfData = transformDataPool_[frameIndex];

            matResources.resize(kMaxSpriteCount);
            tfResources.resize(kMaxSpriteCount);
            matData.resize(kMaxSpriteCount);
            tfData.resize(kMaxSpriteCount);

            for (size_t i = 0; i < kMaxSpriteCount; ++i) {
                // マテリアル用定数バッファを作成してマップ
                matResources[i] = resourceFactory_->CreateBufferResource(dxCommon_->GetDevice(), sizeof(SpriteMaterial));
                // 永続マッピング（D3D12_HEAP_TYPE_UPLOADでは推奨される方法）
                // Microsoft公式: UPLOAD_BUFFERは永続的にマップしたままにするべき
                // https://learn.microsoft.com/en-us/windows/win32/direct3d12/upload-and-readback-of-resources
                matResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&matData[i]));

                // トランスフォーム用定数バッファを作成してマップ
                tfResources[i] = resourceFactory_->CreateBufferResource(dxCommon_->GetDevice(), sizeof(TransformationMatrix));
                // 永続マッピング（D3D12_HEAP_TYPE_UPLOADでは推奨される方法）
                tfResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&tfData[i]));
            }
        }
    }

    void SpriteRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        UINT frameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

        // フレームが切り替わったときのみバッファインデックスをリセット
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

    void SpriteRenderer::EndPass() {
        // 今は空
    }

    void SpriteRenderer::SetCamera(const ICamera* camera) {
        // スプライトはカメラ不要（2D描画）
        (void)camera;
    }

    size_t SpriteRenderer::GetAvailableConstantBuffer() {
        // スプライト数の上限チェック
        if (currentBufferIndex_ >= kMaxSpriteCount) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: Sprite count exceeded maximum! Increase kMaxSpriteCount or reduce sprite count.\n");
#endif
            // 上限に達した場合は最後のバッファを再利用（クラッシュ防止）
            return kMaxSpriteCount - 1;
        }

        size_t bufferIndex = currentBufferIndex_;
        currentBufferIndex_++;
        return bufferIndex;
    }

    Matrix4x4 SpriteRenderer::CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const {
        // ワールド変換
        Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, position);

        // ビュー変換（2Dなので単位行列）
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();

        // 射影変換（正射影）- スプライト用座標系
        // 左上原点(0,0)から右下(width,height)の座標系
        Matrix4x4 projectionMatrix = MathCore::Rendering::Orthographic(
            0.0f, 0.0f,
            static_cast<float>(WinApp::kClientWidth),
            static_cast<float>(WinApp::kClientHeight),
            0.0f, 100.0f);

        return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
    }

    Matrix4x4 SpriteRenderer::CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation, const ICamera* camera) const {
        // ワールド変換
        Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, position);

        if (camera) {
            // カメラのビュー・プロジェクション行列を使用
            Matrix4x4 viewMatrix = camera->GetViewMatrix();
            Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
            return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
        } else {
            // カメラがない場合は従来の方式（スクリーン座標固定）
            Matrix4x4 viewMatrix = MathCore::Matrix::Identity();
            Matrix4x4 projectionMatrix = MathCore::Rendering::Orthographic(
                0.0f, 0.0f,
                static_cast<float>(WinApp::kClientWidth),
                static_cast<float>(WinApp::kClientHeight),
                0.0f, 100.0f);
            return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
        }
    }
}

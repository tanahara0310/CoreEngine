#include "pch.h"
#include "BaseParticleRenderer.h"
#include "Particle/ParticleSystem.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Camera/Camera.h"
#include <cassert>


namespace CoreEngine
{
    void BaseParticleRenderer::Initialize(ID3D12Device* device) {
        device_ = device;

        // リソースファクトリが設定されているか確認
        assert(resourceFactory_ != nullptr && "ResourceFactory must be set before initialization");

        // BaseRenderer から継承したサブシステムはすでに初期化済み

        // シェーダーコンパイラの初期化
        shaderCompiler_->Initialize();

        // ルートシグネチャの作成（共通処理）
        CreateRootSignature();

        // パイプラインステートオブジェクトの作成（派生クラスで実装）
        CreatePSO();
    }

    void BaseParticleRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        cmdList_ = cmdList;

        // ルートシグネチャとパイプラインステートを設定
        cmdList_->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList_->SetPipelineState(psoMg_->GetPipelineState(blendMode));

        // プリミティブトポロジを設定
        cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 派生クラスでの追加処理
        OnBeginPass();
    }

    void BaseParticleRenderer::EndPass() {
        cmdList_ = nullptr;
    }

    void BaseParticleRenderer::SetCamera(const Camera* camera) {
        camera_ = camera;
    }

    void BaseParticleRenderer::CreateRootSignature() {
        // パーティクル用シェーダーをコンパイルしてリフレクション
        // （自前のシェーダーを持つ派生クラスではそちらが使われる）
        auto vertexShaderBlob = shaderCompiler_->CompileShader(GetVertexShaderPath(), L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(GetPixelShaderPath(), L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(
            vertexShaderBlob, pixelShaderBlob, RenderPassTypeToString(GetRenderPassType()));

        // シンプルな設定でRootSignatureを構築
        RootSignatureConfig config;
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());

        auto buildResult = rootSignatureMg_->Build(device_, *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Particle Root Signature: " + buildResult.errorMessage);
        }
    }

    int BaseParticleRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    bool BaseParticleRenderer::ValidateDrawCall(ParticleSystem* particle) const {
        if (!cmdList_ || !particle || !particle->IsActive()) {
            return false;
        }

        uint32_t instanceCount = particle->GetInstanceCount();
        if (instanceCount == 0) {
            return false;
        }

        return true;
    }

    void BaseParticleRenderer::SetupCommonResources(ParticleSystem* particle, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {
        // インスタンシングリソースを設定
        int instanceIdx = GetRootParamIndex("gParticle");
        if (instanceIdx >= 0) {
            cmdList_->SetGraphicsRootDescriptorTable(instanceIdx, particle->GetInstancingSrvHandleGPU());
        }

        // テクスチャを設定
        int texIdx = GetRootParamIndex("gTexture");
        if (texIdx >= 0) {
            cmdList_->SetGraphicsRootDescriptorTable(texIdx, textureHandle);
        }

        // フォグ（減衰のみのバリアント。FogPass が毎フレーム供給する）
        if (fogCBV_ != 0) {
            const int fogIdx = GetRootParamIndex("gFog");
            if (fogIdx >= 0) {
                cmdList_->SetGraphicsRootConstantBufferView(fogIdx, fogCBV_);
            }
        }
    }
}

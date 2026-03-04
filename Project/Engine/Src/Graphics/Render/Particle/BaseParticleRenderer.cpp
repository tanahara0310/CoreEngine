#include "BaseParticleRenderer.h"
#include "Particle/ParticleSystem.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Camera/ICamera.h"
#include <cassert>


namespace CoreEngine
{
    void BaseParticleRenderer::Initialize(ID3D12Device* device) {
        device_ = device;

        // リソースファクトリが設定されているか確認
        assert(resourceFactory_ != nullptr && "ResourceFactory must be set before initialization");

        // パイプラインとシェーダーマネージャーの初期化
        pipelineMg_ = std::make_unique<PipelineStateManager>();
        rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        shaderCompiler_ = std::make_unique<ShaderCompiler>();
        reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

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
        cmdList_->SetPipelineState(pipelineMg_->GetPipelineState(blendMode));

        // プリミティブトポロジを設定
        cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 派生クラスでの追加処理
        OnBeginPass();
    }

    void BaseParticleRenderer::EndPass() {
        cmdList_ = nullptr;
    }

    void BaseParticleRenderer::SetCamera(const ICamera* camera) {
        camera_ = camera;
    }

    void BaseParticleRenderer::CreateRootSignature() {
        // パーティクル用シェーダーをコンパイルしてリフレクション
        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "ParticleRenderer");

        // シンプルな設定でRootSignatureを構築
        RootSignatureConfig config = RootSignatureConfig::Simple();
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
    }
}

#include "pch.h"
#include "BaseParticleRenderer.h"
#include "Particle/ParticleSystemComponent.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Render/Particle/ParticleBindings.h"
#include "Camera/Camera.h"
#include <cassert>


namespace CoreEngine
{
    void BaseParticleRenderer::Initialize(ID3D12Device* device) {
        device_ = device;


        // BaseRenderer から継承したサブシステムはすでに初期化済み

        // ルートシグネチャの作成（共通処理。シェーダーコンパイラもここで初期化される）
        CreateRootSignature();

        // パイプラインステートオブジェクトの作成（派生クラスで実装）
        CreatePSO();
    }

    void BaseParticleRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        cmdList_ = cmdList;
        currentBlendMode_ = blendMode;

        BindPassPipeline(cmdList_, rootSignatureMg_->GetRootSignature(), psoMg_->GetPipelineState(blendMode));

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
        RootSignatureConfig config;
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());

        const std::string name = RenderPassTypeToString(GetRenderPassType());
        BuildShadersAndRootSignature(device_, GetVertexShaderPath(), GetPixelShaderPath(), name, config, "Particle");
        bindings_ = BindingTable::Resolve(*reflectionData_, ParticleBind::kDecls, name);
    }

    bool BaseParticleRenderer::ValidateDrawCall(ParticleSystemComponent* particle) const {
        if (!cmdList_ || !particle) {
            return false;
        }

        uint32_t instanceCount = particle->GetInstanceCount();
        if (instanceCount == 0) {
            return false;
        }

        return true;
    }

    void BaseParticleRenderer::SetupCommonResources(ParticleSystemComponent* particle, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {
        BindDrawResources(particle->GetInstancingSrvHandleGPU(), textureHandle);
    }

    void BaseParticleRenderer::BindDrawResources(
        D3D12_GPU_DESCRIPTOR_HANDLE instancingSrv, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {
        ShaderBinder binder(cmdList_, ShaderBinder::Pipeline::Graphics);

        // インスタンシングリソースを設定
        binder.Set(bindings_[ParticleBind::gParticle], instancingSrv);

        // テクスチャを設定
        binder.Set(bindings_[ParticleBind::gTexture], textureHandle);

        BindFogResources(binder);
        binder.ValidateBeforeDraw(bindings_);
    }

    void BaseParticleRenderer::BindFogResources(ShaderBinder& binder) {
        // 合成なしは全画面の合成が深度から掛けるので恒等、それ以外は減衰のみ（FogPass が毎フレーム供給する）
        const D3D12_GPU_VIRTUAL_ADDRESS fogCBV =
            (currentBlendMode_ == BlendMode::kBlendModeNone) ? fogDisabledCBV_ : fogCBV_;
        if (fogCBV != 0) {
            binder.Set(bindings_[ParticleBind::gFog], fogCBV);
        }
        // ボリュメトリックの格子（定数が格子を使うビューだけ）
        if (fogVolumetric_.IsValid()) {
            binder.Set(bindings_[ParticleBind::gVolumetricFogIntegrated], fogVolumetric_.integratedSrv);
            binder.Set(bindings_[ParticleBind::gVolumetricFogFarLight], fogVolumetric_.farLightSrv);
            binder.Set(bindings_[ParticleBind::gVolumetricFogFarMedia], fogVolumetric_.farMediaCbv);
        }
    }
}

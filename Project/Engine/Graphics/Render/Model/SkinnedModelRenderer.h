#pragma once
#include "Engine/Graphics/Render/IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflectionBuilder.h"
#include "Engine/Graphics/RootSignature/RootSignatureConfig.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

// 前方宣言
namespace CoreEngine {
    class LightManager;
    class ShaderReflectionData;
}

namespace CoreEngine
{
    /// @brief スキニングモデル描画用レンダラー
    class SkinnedModelRenderer : public IRenderer {
    public:
        void Initialize(ID3D12Device* device) override;
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }
        void SetCamera(const ICamera* camera) override;

        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        void SetLightManager(LightManager* lightManager) { lightManager_ = lightManager; }
        void SetEnvironmentMap(D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle) { environmentMapHandle_ = environmentMapHandle; }
        void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle) { shadowMapHandle_ = shadowMapHandle; }
        void SetLightViewProjection(D3D12_GPU_VIRTUAL_ADDRESS lightVPAddress) { lightViewProjectionCBV_ = lightVPAddress; }
        void SetIrradianceMap(D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle) { irradianceMapHandle_ = irradianceMapHandle; }
        void SetPrefilteredMap(D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle) { prefilteredMapHandle_ = prefilteredMapHandle; }
        void SetBRDFLUT(D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle) { brdfLUTHandle_ = brdfLUTHandle; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        /// @param resourceName シェーダーで定義されたリソース名（例: "gMaterial", "gMatrixPalette"）
        /// @return ルートパラメータインデックス（見つからない場合は-1）
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        ID3D12PipelineState* pipelineState_ = nullptr;
        BlendMode currentBlendMode_;
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBV_ = 0;

        CoreEngine::LightManager* lightManager_ = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle_ = {};
        D3D12_GPU_VIRTUAL_ADDRESS lightViewProjectionCBV_ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle_ = {};

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;
    };
}

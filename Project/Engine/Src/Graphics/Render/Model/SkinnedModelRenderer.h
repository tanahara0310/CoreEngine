#pragma once
#include "Graphics/Render/IRenderer.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Math/Vector/Vector3.h"
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
        void BeginGBufferPass(ID3D12GraphicsCommandList* cmdList);
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }
        void SetCamera(const ICamera* camera) override;

        ID3D12RootSignature* GetRootSignature() const { return forwardRootSignatureMg_->GetRootSignature(); }
        ID3D12RootSignature* GetGBufferRootSignature() const { return gBufferRootSignatureMg_->GetRootSignature(); }

        void SetLightManager(LightManager* lightManager) { lightManager_ = lightManager; }
        void SetEnvironmentMap(D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle) { environmentMapHandle_ = environmentMapHandle; }
        void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle) { shadowMapHandle_ = shadowMapHandle; }
        void SetLightViewProjection(D3D12_GPU_VIRTUAL_ADDRESS lightVPAddress) { lightViewProjectionCBV_ = lightVPAddress; }
        void SetIrradianceMap(D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle) { irradianceMapHandle_ = irradianceMapHandle; }
        void SetPrefilteredMap(D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle) { prefilteredMapHandle_ = prefilteredMapHandle; }
        void SetBRDFLUT(D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle) { brdfLUTHandle_ = brdfLUTHandle; }

        /// @brief シーン共通 IBL 環境回転角度を設定（ラジアン）
        void SetIBLRotation(const Vector3& rotation) { iblRotation_ = rotation; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        /// @param resourceName シェーダーで定義されたリソース名（例: "gMaterial", "gMatrixPalette"）
        /// @return ルートパラメータインデックス（見つからない場合は-1）
        int GetRootParamIndex(const std::string& resourceName) const;
        int GetGBufferRootParamIndex(const std::string& resourceName) const;
        bool IsInGBufferPass() const { return isInGBufferPass_; }

    private:
        std::unique_ptr<RootSignatureManager> forwardRootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<RootSignatureManager> gBufferRootSignatureMg_  = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> forwardPsoMg_            = std::make_unique<PipelineStateManager>();
        std::unique_ptr<PipelineStateManager> gBufferPsoMg_            = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler>        shaderCompiler_         = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_    = std::make_unique<ShaderReflectionBuilder>();

        ID3D12PipelineState* forwardPipelineState_ = nullptr;
        ID3D12PipelineState* gBufferPipelineState_ = nullptr;
        BlendMode currentBlendMode_;
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBV_ = 0;

        CoreEngine::LightManager* lightManager_ = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle_      = {};
        D3D12_GPU_VIRTUAL_ADDRESS   lightViewProjectionCBV_ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle_  = {};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle_        = {};

        // IBL シーンパラメータ定数バッファ（environmentRotation）
        Microsoft::WRL::ComPtr<ID3D12Resource> iblParamsBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS              iblParamsCBVAddress_ = 0;
        Vector3 iblRotation_ = {};

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> forwardReflectionData_;
        std::unique_ptr<ShaderReflectionData> gBufferReflectionData_;
        bool isInGBufferPass_ = false;
    };
}

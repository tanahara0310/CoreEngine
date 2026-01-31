#pragma once
#include "Engine/Graphics/Render/IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

// 前方宣言
namespace CoreEngine {
	class LightManager;
}

// Root Parameter インデックス定数

namespace CoreEngine
{
	namespace SkinnedModelRendererRootParam {
	static constexpr UINT kWVP = 0;                   // b0: TransformationMatrix (VS)
	static constexpr UINT kMatrixPalette = 1;         // t0: MatrixPalette (VS)
	static constexpr UINT kMaterial = 2;              // b0: Material (PS)
	static constexpr UINT kTexture = 3;               // t0: Texture (PS)
	static constexpr UINT kLightCounts = 4;           // b1: LightCounts (PS)
	static constexpr UINT kCamera = 5;                // b2: Camera (PS)
	static constexpr UINT kDirectionalLights = 6;     // t1: DirectionalLights (PS)
	static constexpr UINT kPointLights = 7;           // t2: PointLights (PS)
	static constexpr UINT kSpotLights = 8;            // t3: SpotLights (PS)
	static constexpr UINT kAreaLights = 9;            // t4: AreaLights (PS)
	static constexpr UINT kEnvironmentMap = 10;       // t5: EnvironmentMap (PS)
	static constexpr UINT kLightViewProjection = 11;  // b1: LightViewProjection (VS)
	static constexpr UINT kShadowMap = 12;            // t6: ShadowMap (PS)
	static constexpr UINT kNormalMap = 13;            // t7: NormalMap (PS)
	static constexpr UINT kMetallicMap = 14;          // t8: MetallicMap (PS)
	static constexpr UINT kRoughnessMap = 15;         // t9: RoughnessMap (PS)
	static constexpr UINT kAOMap = 16;                // t10: AOMap (PS)
	static constexpr UINT kIrradianceMap = 17;        // t11: IrradianceMap (PS)
	static constexpr UINT kPrefilteredMap = 18;       // t12: PrefilteredMap (PS)
	static constexpr UINT kBRDFLUT = 19;              // t13: BRDF LUT (PS)
	}

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

private:
	std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
	std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
	std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();

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
};
}

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
namespace ModelRendererRootParam {
	static constexpr UINT kMaterial = 0;              // b0: Material (PS)
	static constexpr UINT kWVP = 1;                   // b0: TransformationMatrix (VS)
	static constexpr UINT kTexture = 2;               // t0: Texture (PS)
	static constexpr UINT kLightCounts = 3;           // b1: LightCounts (PS)
	static constexpr UINT kCamera = 4;                // b2: Camera (PS)
	static constexpr UINT kDirectionalLights = 5;     // t1: DirectionalLights (PS)
	static constexpr UINT kPointLights = 6;           // t2: PointLights (PS)
	static constexpr UINT kSpotLights = 7;            // t3: SpotLights (PS)
	static constexpr UINT kAreaLights = 8;            // t4: AreaLights (PS)
	static constexpr UINT kEnvironmentMap = 9;        // t5: EnvironmentMap (PS)
}

/// @brief 通常モデル描画用レンダラー
class ModelRenderer : public IRenderer {
public:
	void Initialize(ID3D12Device* device) override;
	void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
	void EndPass() override;
	RenderPassType GetRenderPassType() const override { return RenderPassType::Model; }
	void SetCamera(const ICamera* camera) override;
	
	ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

	void SetLightManager(class LightManager* lightManager) { lightManager_ = lightManager; }
	void SetEnvironmentMap(D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle) { environmentMapHandle_ = environmentMapHandle; }
	
private:
	std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
	std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
	std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
	
	ID3D12PipelineState* pipelineState_ = nullptr;
	BlendMode currentBlendMode_;
	D3D12_GPU_VIRTUAL_ADDRESS cameraCBV_ = 0;

	CoreEngine::LightManager* lightManager_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle_ = {};
};
}

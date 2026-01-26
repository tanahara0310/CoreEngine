#include "ModelRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Light/LightManager.h"
#include <cassert>


namespace CoreEngine
{
	void ModelRenderer::Initialize(ID3D12Device* device) {

		shaderCompiler_->Initialize();

		// RootSignatureの初期化

		// Root Parameter 0: マテリアル用CBV (b0, PS)
		RootSignatureManager::RootDescriptorConfig materialCBV;
		materialCBV.shaderRegister = 0;
		materialCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootSignatureMg_->AddRootCBV(materialCBV);

		// Root Parameter 1: トランスフォーム用CBV (b0, VS)
		RootSignatureManager::RootDescriptorConfig transformCBV;
		transformCBV.shaderRegister = 0;
		transformCBV.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootSignatureMg_->AddRootCBV(transformCBV);

		// Root Parameter 2: テクスチャ用ディスクリプタテーブル (t0, PS)
		RootSignatureManager::DescriptorRangeConfig textureRange;
		textureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		textureRange.numDescriptors = 1;
		textureRange.baseShaderRegister = 0;
		rootSignatureMg_->AddDescriptorTable({ textureRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 3: ライトカウント用CBV (b1, PS)
		RootSignatureManager::RootDescriptorConfig lightCountsCBV;
		lightCountsCBV.shaderRegister = 1;
		lightCountsCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootSignatureMg_->AddRootCBV(lightCountsCBV);

		// Root Parameter 4: カメラ用CBV (b2, PS)
		RootSignatureManager::RootDescriptorConfig cameraCBV;
		cameraCBV.shaderRegister = 2;
		cameraCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootSignatureMg_->AddRootCBV(cameraCBV);

		// Root Parameter 5: ディレクショナルライト用ディスクリプタテーブル (t1, PS)
		RootSignatureManager::DescriptorRangeConfig directionalLightsRange;
		directionalLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		directionalLightsRange.numDescriptors = 1;
		directionalLightsRange.baseShaderRegister = 1;
		rootSignatureMg_->AddDescriptorTable({ directionalLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 6: ポイントライト用ディスクリプタテーブル (t2, PS)
		RootSignatureManager::DescriptorRangeConfig pointLightsRange;
		pointLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		pointLightsRange.numDescriptors = 1;
		pointLightsRange.baseShaderRegister = 2;
		rootSignatureMg_->AddDescriptorTable({ pointLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 7: スポットライト用ディスクリプタテーブル (t3, PS)
		RootSignatureManager::DescriptorRangeConfig spotLightsRange;
		spotLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		spotLightsRange.numDescriptors = 1;
		spotLightsRange.baseShaderRegister = 3;
		rootSignatureMg_->AddDescriptorTable({ spotLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 8: エリアライト用ディスクリプタテーブル (t4, PS)
		RootSignatureManager::DescriptorRangeConfig areaLightsRange;
		areaLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		areaLightsRange.numDescriptors = 1;
		areaLightsRange.baseShaderRegister = 4;
		rootSignatureMg_->AddDescriptorTable({ areaLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 9: 環境マップ用ディスクリプタテーブル (t5, PS)
		RootSignatureManager::DescriptorRangeConfig environmentMapRange;
		environmentMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		environmentMapRange.numDescriptors = 1;
		environmentMapRange.baseShaderRegister = 5;
		rootSignatureMg_->AddDescriptorTable({ environmentMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 10: LightViewProjection用CBV (b1, VS)
		RootSignatureManager::RootDescriptorConfig lightVPCBV;
		lightVPCBV.shaderRegister = 1;
		lightVPCBV.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootSignatureMg_->AddRootCBV(lightVPCBV);

		// Root Parameter 11: ShadowMap用ディスクリプタテーブル (t6, PS)
		RootSignatureManager::DescriptorRangeConfig shadowMapRange;
		shadowMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		shadowMapRange.numDescriptors = 1;
		shadowMapRange.baseShaderRegister = 6;
		rootSignatureMg_->AddDescriptorTable({ shadowMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Static Sampler (s0, PS)
		rootSignatureMg_->AddDefaultLinearSampler(0, D3D12_SHADER_VISIBILITY_PIXEL);

		// Static Sampler (s1, PS) - Comparison Sampler for Shadow Map
		RootSignatureManager::StaticSamplerConfig shadowSampler;
		shadowSampler.shaderRegister = 1;
		shadowSampler.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		shadowSampler.filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		shadowSampler.comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		shadowSampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		shadowSampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		shadowSampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		shadowSampler.borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		rootSignatureMg_->AddStaticSampler(shadowSampler);

		rootSignatureMg_->Create(device);

		// シェーダーのコンパイルとPSO作成
		auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Object/Object3d.VS.hlsl", L"vs_6_0");
		assert(vertexShaderBlob != nullptr);

		auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
		assert(pixelShaderBlob != nullptr);

		bool result = psoMg_->CreateBuilder()
			.AddInputElement("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
			.AddInputElement("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
			.AddInputElement("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
			.SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
			.SetDepthStencil(true, true)
			.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
			.BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

		if (!result) {
			throw std::runtime_error("Failed to create Pipeline State Object");
		}

		pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
	}

	void ModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {

		if (blendMode != currentBlendMode_) {
			currentBlendMode_ = blendMode;
			pipelineState_ = psoMg_->GetPipelineState(blendMode);
		}

		cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
		cmdList->SetPipelineState(pipelineState_);
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// カメラCBVを設定
		if (cameraCBV_ != 0) {
			cmdList->SetGraphicsRootConstantBufferView(ModelRendererRootParam::kCamera, cameraCBV_);
		}

		// ライトをセット
		if (lightManager_) {
			lightManager_->SetLightsToCommandList(
				cmdList,
				ModelRendererRootParam::kLightCounts,
				ModelRendererRootParam::kDirectionalLights,
				ModelRendererRootParam::kPointLights,
				ModelRendererRootParam::kSpotLights,
				ModelRendererRootParam::kAreaLights
			);
		}

		// 環境マップをセット
		if (environmentMapHandle_.ptr != 0) {
			cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kEnvironmentMap, environmentMapHandle_);
		}

		// ライトビュープロジェクション行列をセット（未設定の場合はスキップ）
		// 注意: シェーダーがこのリソースを期待している場合、ダミーデータが必要
		if (lightViewProjectionCBV_ != 0) {
			cmdList->SetGraphicsRootConstantBufferView(ModelRendererRootParam::kLightViewProjection, lightViewProjectionCBV_);
		}

	// シャドウマップをセット（未設定の場合はスキップ）
	// 注意: シェーダーがこのリソースを期待している場合、ダミーテクスチャが必要
	if (shadowMapHandle_.ptr != 0) {
		cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kShadowMap, shadowMapHandle_);
	}
}

	void ModelRenderer::EndPass() {
	}

	void ModelRenderer::SetCamera(const ICamera* camera) {
		if (camera) {
			cameraCBV_ = camera->GetGPUVirtualAddress();
		} else {
			cameraCBV_ = 0;
		}
	}
}

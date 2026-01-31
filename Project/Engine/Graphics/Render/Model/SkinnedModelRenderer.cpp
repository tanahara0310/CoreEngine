#include "SkinnedModelRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Light/LightManager.h"
#include <cassert>


namespace CoreEngine
{
	void SkinnedModelRenderer::Initialize(ID3D12Device* device) {

		shaderCompiler_->Initialize();

		// RootSignatureの初期化

		// Root Parameter 0: トランスフォーム用CBV (b0, VS)
		RootSignatureManager::RootDescriptorConfig skinningTransformCBV;
		skinningTransformCBV.shaderRegister = 0;
		skinningTransformCBV.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootSignatureMg_->AddRootCBV(skinningTransformCBV);

		// Root Parameter 1: MatrixPalette用ディスクリプタテーブル (t0, VS)
		RootSignatureManager::DescriptorRangeConfig matrixPaletteRange;
		matrixPaletteRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		matrixPaletteRange.numDescriptors = 1;
		matrixPaletteRange.baseShaderRegister = 0;
		rootSignatureMg_->AddDescriptorTable({ matrixPaletteRange }, D3D12_SHADER_VISIBILITY_VERTEX);

		// Root Parameter 2: マテリアル用CBV (b0, PS)
		RootSignatureManager::RootDescriptorConfig skinningMaterialCBV;
		skinningMaterialCBV.shaderRegister = 0;
		skinningMaterialCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootSignatureMg_->AddRootCBV(skinningMaterialCBV);

		// Root Parameter 3: テクスチャ用ディスクリプタテーブル (t0, PS)
		RootSignatureManager::DescriptorRangeConfig skinningTextureRange;
		skinningTextureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		skinningTextureRange.numDescriptors = 1;
		skinningTextureRange.baseShaderRegister = 0;
		rootSignatureMg_->AddDescriptorTable({ skinningTextureRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 4: ライトカウント用CBV (b1, PS)
		RootSignatureManager::RootDescriptorConfig lightCountsCBV;
		lightCountsCBV.shaderRegister = 1;
		lightCountsCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootSignatureMg_->AddRootCBV(lightCountsCBV);

		// Root Parameter 5: カメラ用CBV (b2, PS)
		RootSignatureManager::RootDescriptorConfig skinningCameraCBV;
		skinningCameraCBV.shaderRegister = 2;
		skinningCameraCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootSignatureMg_->AddRootCBV(skinningCameraCBV);

		// Root Parameter 6: ディレクショナルライト用ディスクリプタテーブル (t1, PS)
		RootSignatureManager::DescriptorRangeConfig directionalLightsRange;
		directionalLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		directionalLightsRange.numDescriptors = 1;
		directionalLightsRange.baseShaderRegister = 1;
		rootSignatureMg_->AddDescriptorTable({ directionalLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 7: ポイントライト用ディスクリプタテーブル (t2, PS)
		RootSignatureManager::DescriptorRangeConfig pointLightsRange;
		pointLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		pointLightsRange.numDescriptors = 1;
		pointLightsRange.baseShaderRegister = 2;
		rootSignatureMg_->AddDescriptorTable({ pointLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 8: スポットライト用ディスクリプタテーブル (t3, PS)
		RootSignatureManager::DescriptorRangeConfig spotLightsRange;
		spotLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		spotLightsRange.numDescriptors = 1;
		spotLightsRange.baseShaderRegister = 3;
		rootSignatureMg_->AddDescriptorTable({ spotLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 9: エリアライト用ディスクリプタテーブル (t4, PS)
		RootSignatureManager::DescriptorRangeConfig areaLightsRange;
		areaLightsRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		areaLightsRange.numDescriptors = 1;
		areaLightsRange.baseShaderRegister = 4;
		rootSignatureMg_->AddDescriptorTable({ areaLightsRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 10: 環境マップ用ディスクリプタテーブル (t5, PS)
		RootSignatureManager::DescriptorRangeConfig environmentMapRange;
		environmentMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		environmentMapRange.numDescriptors = 1;
		environmentMapRange.baseShaderRegister = 5;
		rootSignatureMg_->AddDescriptorTable({ environmentMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

		// Root Parameter 11: LightViewProjection用CBV (b1, VS)
		RootSignatureManager::RootDescriptorConfig lightVPCBV;
		lightVPCBV.shaderRegister = 1;
		lightVPCBV.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootSignatureMg_->AddRootCBV(lightVPCBV);

	// Root Parameter 12: ShadowMap用ディスクリプタテーブル (t6, PS)
	RootSignatureManager::DescriptorRangeConfig shadowMapRange;
	shadowMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowMapRange.numDescriptors = 1;
	shadowMapRange.baseShaderRegister = 6;
	rootSignatureMg_->AddDescriptorTable({ shadowMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 13: PBR NormalMap用ディスクリプタテーブル (t7, PS)
	RootSignatureManager::DescriptorRangeConfig normalMapRange;
	normalMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	normalMapRange.numDescriptors = 1;
	normalMapRange.baseShaderRegister = 7;
	rootSignatureMg_->AddDescriptorTable({ normalMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 14: PBR MetallicMap用ディスクリプタテーブル (t8, PS)
	RootSignatureManager::DescriptorRangeConfig metallicMapRange;
	metallicMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	metallicMapRange.numDescriptors = 1;
	metallicMapRange.baseShaderRegister = 8;
	rootSignatureMg_->AddDescriptorTable({ metallicMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 15: PBR RoughnessMap用ディスクリプタテーブル (t9, PS)
	RootSignatureManager::DescriptorRangeConfig roughnessMapRange;
	roughnessMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	roughnessMapRange.numDescriptors = 1;
	roughnessMapRange.baseShaderRegister = 9;
	rootSignatureMg_->AddDescriptorTable({ roughnessMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 16: PBR AOMap用ディスクリプタテーブル (t10, PS)
	RootSignatureManager::DescriptorRangeConfig aoMapRange;
	aoMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	aoMapRange.numDescriptors = 1;
	aoMapRange.baseShaderRegister = 10;
	rootSignatureMg_->AddDescriptorTable({ aoMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 17: IBL IrradianceMap用ディスクリプタテーブル (t11, PS)
	RootSignatureManager::DescriptorRangeConfig irradianceMapRange;
	irradianceMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	irradianceMapRange.numDescriptors = 1;
	irradianceMapRange.baseShaderRegister = 11;
	rootSignatureMg_->AddDescriptorTable({ irradianceMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 18: IBL PrefilteredMap用ディスクリプタテーブル (t12, PS)
	RootSignatureManager::DescriptorRangeConfig prefilteredMapRange;
	prefilteredMapRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	prefilteredMapRange.numDescriptors = 1;
	prefilteredMapRange.baseShaderRegister = 12;
	rootSignatureMg_->AddDescriptorTable({ prefilteredMapRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Root Parameter 19: IBL BRDF LUT用ディスクリプタテーブル (t13, PS)
	RootSignatureManager::DescriptorRangeConfig brdfLUTRange;
	brdfLUTRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	brdfLUTRange.numDescriptors = 1;
	brdfLUTRange.baseShaderRegister = 13;
	rootSignatureMg_->AddDescriptorTable({ brdfLUTRange }, D3D12_SHADER_VISIBILITY_PIXEL);

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
		auto skinningVertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Skinning/SkinningObject3d.VS.hlsl", L"vs_6_0");
		assert(skinningVertexShaderBlob != nullptr);

		auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
		assert(pixelShaderBlob != nullptr);

		bool skinningResult = psoMg_->CreateBuilder()
			.AddInputElement("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 0)
			.AddInputElement("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 0)
			.AddInputElement("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 0)
			.AddInputElement("WEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 1)
			.AddInputElement("INDEX", 0, DXGI_FORMAT_R32G32B32A32_SINT, D3D12_APPEND_ALIGNED_ELEMENT, 1)
			.SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
			.SetDepthStencil(true, true)
			.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
			.BuildAllBlendModes(device, skinningVertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

		if (!skinningResult) {
			throw std::runtime_error("Failed to create Skinning Pipeline State Object");
		}

		pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
	}

	void SkinnedModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {

		if (blendMode != currentBlendMode_) {
			currentBlendMode_ = blendMode;
			pipelineState_ = psoMg_->GetPipelineState(blendMode);
		}

		cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
		cmdList->SetPipelineState(pipelineState_);
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (cameraCBV_ != 0) {
			cmdList->SetGraphicsRootConstantBufferView(SkinnedModelRendererRootParam::kCamera, cameraCBV_);
		}


		if (lightManager_) {
			lightManager_->SetLightsToCommandList(
				cmdList,
				SkinnedModelRendererRootParam::kLightCounts,
				SkinnedModelRendererRootParam::kDirectionalLights,
				SkinnedModelRendererRootParam::kPointLights,
				SkinnedModelRendererRootParam::kSpotLights,
				SkinnedModelRendererRootParam::kAreaLights
			);
		}

		if (environmentMapHandle_.ptr != 0) {
			cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kEnvironmentMap, environmentMapHandle_);
		}

		// ライトビュープロジェクション行列をセット
		if (lightViewProjectionCBV_ != 0) {
			cmdList->SetGraphicsRootConstantBufferView(SkinnedModelRendererRootParam::kLightViewProjection, lightViewProjectionCBV_);
		}

	// シャドウマップをセット
	if (shadowMapHandle_.ptr != 0) {
		cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kShadowMap, shadowMapHandle_);
	}

	// IBL Irradiance Mapをセット（未設定の場合はスキップ）
	if (irradianceMapHandle_.ptr != 0) {
		cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kIrradianceMap, irradianceMapHandle_);
	}

	// IBL Prefiltered Mapをセット（未設定の場合はスキップ）
	if (prefilteredMapHandle_.ptr != 0) {
		cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kPrefilteredMap, prefilteredMapHandle_);
	}

	// BRDF LUTをセット（未設定の場合はスキップ）
	if (brdfLUTHandle_.ptr != 0) {
		cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kBRDFLUT, brdfLUTHandle_);
	}
}

	void SkinnedModelRenderer::EndPass() {
	}

	void SkinnedModelRenderer::SetCamera(const ICamera* camera) {
		if (camera) {
			cameraCBV_ = camera->GetGPUVirtualAddress();
		} else {
			cameraCBV_ = 0;
		}
	}
}

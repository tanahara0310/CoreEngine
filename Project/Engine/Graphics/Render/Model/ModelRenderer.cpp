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

		// Static Sampler (s0, PS)
		rootSignatureMg_->AddDefaultLinearSampler(0, D3D12_SHADER_VISIBILITY_PIXEL);

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
				ModelRendererRootParam::kSpotLights
			);
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

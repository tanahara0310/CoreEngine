#include "ShadowMapRenderer.h"
#include "Engine/Camera/ICamera.h"
#include <cassert>
#include <DirectXMath.h>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace CoreEngine
{
	void ShadowMapRenderer::Initialize(ID3D12Device* device) {

		device_ = device;
		shaderCompiler_->Initialize();

		// ===================================
		// Root Signature の作成
		// ===================================

		// Root Parameter 0: LightTransform用CBV (b0, VS)
		RootSignatureManager::RootDescriptorConfig lightTransformCBV;
		lightTransformCBV.shaderRegister = 0;
		lightTransformCBV.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootSignatureMg_->AddRootCBV(lightTransformCBV);

		// Root Parameter 1: MatrixPalette用ディスクリプタテーブル (t0, VS) - スキニング用
		RootSignatureManager::DescriptorRangeConfig matrixPaletteRange;
		matrixPaletteRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		matrixPaletteRange.numDescriptors = 1;
		matrixPaletteRange.baseShaderRegister = 0;
		rootSignatureMg_->AddDescriptorTable({ matrixPaletteRange }, D3D12_SHADER_VISIBILITY_VERTEX);

		rootSignatureMg_->Create(device);

		// PSOを作成
		CreatePipelineStates();

		// デフォルトは通常モデル用PSOを使用
		currentPipelineState_ = normalModelPSO_->GetPipelineState(BlendMode::kBlendModeNone);

		// ライトビュープロジェクション行列を単位行列で初期化
		lightViewProjection_ = CoreEngine::MathCore::Matrix::Identity();
	}

	void ShadowMapRenderer::CreatePipelineStates() {
		// ===================================
		// 通常モデル用 Pipeline State 作成
		// ===================================

		auto normalVertexShader = shaderCompiler_->CompileShader(L"Assets/Shaders/Shadow/ShadowMap.VS.hlsl", L"vs_6_0");
		assert(normalVertexShader != nullptr);

		bool normalResult = normalModelPSO_->CreateBuilder()
			.AddInputElement("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
			.SetRasterizer(D3D12_CULL_MODE_FRONT, D3D12_FILL_MODE_SOLID)
			.SetDepthBias(biasSettings_.depthBias, biasSettings_.slopeScaledDepthBias, biasSettings_.depthBiasClamp)
			.SetDepthStencil(true, true, D3D12_COMPARISON_FUNC_LESS)
			.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
			.SetRenderTargetFormat(DXGI_FORMAT_UNKNOWN, 0)
			.SetDepthStencilFormat(DXGI_FORMAT_D32_FLOAT)
			.Build(device_, normalVertexShader, nullptr, rootSignatureMg_->GetRootSignature());

		if (!normalResult) {
			throw std::runtime_error("Failed to create ShadowMap Pipeline State Object for Normal Model");
		}

		// ===================================
		// スキニングモデル用 Pipeline State 作成
		// ===================================

		auto skinningVertexShader = shaderCompiler_->CompileShader(L"Assets/Shaders/Shadow/ShadowMapSkinning.VS.hlsl", L"vs_6_0");
		assert(skinningVertexShader != nullptr);

		bool skinningResult = skinnedModelPSO_->CreateBuilder()
			.AddInputElement("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 0)
			.AddInputElement("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 0)
			.AddInputElement("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 0)
			.AddInputElement("WEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT, 1)
			.AddInputElement("INDEX", 0, DXGI_FORMAT_R32G32B32A32_SINT, D3D12_APPEND_ALIGNED_ELEMENT, 1)
			.SetRasterizer(D3D12_CULL_MODE_FRONT, D3D12_FILL_MODE_SOLID)
			.SetDepthBias(biasSettings_.depthBias, biasSettings_.slopeScaledDepthBias, biasSettings_.depthBiasClamp)
			.SetDepthStencil(true, true, D3D12_COMPARISON_FUNC_LESS)
			.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
			.SetRenderTargetFormat(DXGI_FORMAT_UNKNOWN, 0)
			.SetDepthStencilFormat(DXGI_FORMAT_D32_FLOAT)
			.Build(device_, skinningVertexShader, nullptr, rootSignatureMg_->GetRootSignature());

		if (!skinningResult) {
			throw std::runtime_error("Failed to create ShadowMap Pipeline State Object for Skinned Model");
		}
	}

	void ShadowMapRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
		(void)blendMode; // 未使用警告を回避
		cmdList_ = cmdList;

		// Root Signatureを設定
		cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());

		// Pipeline Stateを設定
		cmdList->SetPipelineState(currentPipelineState_);

		// トポロジを設定
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// ライトビュープロジェクション行列はオブジェクトごとに設定するため、ここでは設定しない
	}

	void ShadowMapRenderer::EndPass() {
		cmdList_ = nullptr;
	}

	void ShadowMapRenderer::SetCamera(const ICamera* camera) {
		// シャドウマップパスではカメラは使用しない（ライトの視点を使用）
		// 空実装
		(void)camera; // 未使用警告を回避
	}

	void ShadowMapRenderer::SetLightViewProjection(const CoreEngine::Matrix4x4& lightViewProjection) {
		lightViewProjection_ = lightViewProjection;
	}

	void ShadowMapRenderer::SetPSOForNormalModel() {
		currentPipelineState_ = normalModelPSO_->GetPipelineState(BlendMode::kBlendModeNone);
		if (cmdList_) {
			cmdList_->SetPipelineState(currentPipelineState_);
		}
	}

	void ShadowMapRenderer::SetPSOForSkinnedModel() {
		currentPipelineState_ = skinnedModelPSO_->GetPipelineState(BlendMode::kBlendModeNone);
		if (cmdList_) {
			cmdList_->SetPipelineState(currentPipelineState_);
		}
	}

	void ShadowMapRenderer::SetBiasSettings(const ShadowBiasSettings& settings) {
		biasSettings_ = settings;
		// PSOを再作成
		CreatePipelineStates();
		// 現在のPSOを更新
		currentPipelineState_ = normalModelPSO_->GetPipelineState(BlendMode::kBlendModeNone);
	}

#ifdef _DEBUG
	void ShadowMapRenderer::DrawImGui() {
		if (ImGui::CollapsingHeader("Shadow Bias Settings")) {
			bool changed = false;

			changed |= ImGui::DragInt("Depth Bias", &biasSettings_.depthBias, 100, 0, 100000);
			changed |= ImGui::DragFloat("Slope Scaled Bias", &biasSettings_.slopeScaledDepthBias, 0.1f, 0.0f, 10.0f);
			changed |= ImGui::DragFloat("Bias Clamp", &biasSettings_.depthBiasClamp, 0.001f, 0.0f, 1.0f);

			if (changed) {
				SetBiasSettings(biasSettings_);
			}

			if (ImGui::Button("Reset to Default")) {
				SetBiasSettings(ShadowBiasSettings{});
			}
		}
	}
#endif
}

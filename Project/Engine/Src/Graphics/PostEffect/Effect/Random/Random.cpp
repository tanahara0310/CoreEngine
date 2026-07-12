#include "pch.h"
#include "Random.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
	void Random::OnCreateConstantBuffers()
	{
		UINT randomSize = (sizeof(RandomParams) + 255) & ~255;
		randomParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), randomSize);
		HRESULT hr = randomParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedRandomParams_));
		assert(SUCCEEDED(hr));
		UpdateConstantBuffer();

		UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
		screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
		hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
		assert(SUCCEEDED(hr));
	}

	void Random::UpdateConstantBuffer()
	{
		if (mappedRandomParams_) {
			*mappedRandomParams_ = params_;
		}
	}

	void Random::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
	{
		if (mappedScreenParams_) {
			mappedScreenParams_->screenWidth = width;
			mappedScreenParams_->screenHeight = height;
		}
	}

	void Random::SetParams(const RandomParams& params)
	{
		params_ = params;
		UpdateConstantBuffer();
	}

	void Random::Update(float deltaTime)
	{
		accumulatedTime_ += deltaTime * params_.speed;
		params_.time = accumulatedTime_;
		UpdateConstantBuffer();
	}

	void Random::Dispatch(
		D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
		uint32_t width,
		uint32_t height)
	{
		UpdateScreenConstantBuffer(width, height);

		auto* cmdList = directXCommon_->GetCommandList();
		cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
		cmdList->SetPipelineState(computePso_.Get());

		int textureIdx = GetRootParamIndex("gTexture");
		int outputIdx = GetRootParamIndex("gOutput");
		int randomIdx = GetRootParamIndex("RandomParams");
		int screenIdx = GetRootParamIndex("ScreenParams");

		if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
		if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
		if (randomIdx >= 0)  cmdList->SetComputeRootConstantBufferView(randomIdx, randomParamsCB_->GetGPUVirtualAddress());
		if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

		uint32_t groupX = (width + 7) / 8;
		uint32_t groupY = (height + 7) / 8;
		cmdList->Dispatch(groupX, groupY, 1);
	}

	void Random::DrawImGui()
	{
#ifdef USE_IMGUI
		ImGui::PushID("RandomParams");
		ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
		UI::Separator();

		bool changed = false;
		if (ImGui::TreeNode("パラメータ")) {
			changed |= UI::SliderFloat("ノイズ強度", params_.intensity, 0.0f, 1.0f);
			changed |= UI::SliderFloat("ブレンド", params_.blend, 0.0f, 1.0f);
			changed |= UI::SliderFloat("速度", params_.speed, 0.0f, 10.0f);
			changed |= UI::SliderFloat("粒度", params_.grainScale, 0.1f, 8.0f);
			changed |= UI::SliderFloat("輝度影響", params_.luminanceInfluence, 0.0f, 1.0f);
			changed |= UI::SliderFloat("色ノイズ量", params_.chromaAmount, 0.0f, 1.0f);
			ImGui::TreePop();
		}
		if (changed) {
			UpdateConstantBuffer();
		}

		UI::Separator();
		if (ImGui::Button("デフォルトに戻す")) {
			params_ = RandomParams{};
			accumulatedTime_ = 0.0f;
			UpdateConstantBuffer();
		}
		ImGui::PopID();
#endif // USE_IMGUI
	}
}

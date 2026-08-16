#include "pch.h"
#include "Random.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
	namespace
	{
		CVar<float> cvIntensity{
			"r.Random.Intensity", 0.15f,
			"ノイズの強度",
			CVarRange{ 0.0f, 1.0f } };

		CVar<float> cvBlend{
			"r.Random.Blend", 0.35f,
			"元画像とのブレンド率",
			CVarRange{ 0.0f, 1.0f } };

		CVar<float> cvSpeed{
			"r.Random.Speed", 1.0f,
			"ノイズが変化する速さ",
			CVarRange{ 0.0f, 10.0f } };

		CVar<float> cvGrainScale{
			"r.Random.GrainScale", 1.0f,
			"粒の細かさ。大きいほど細かい",
			CVarRange{ 0.1f, 8.0f } };

		CVar<float> cvLuminanceInfluence{
			"r.Random.LuminanceInfluence", 0.25f,
			"元画像の明るさに応じてノイズ量を変える度合い",
			CVarRange{ 0.0f, 1.0f } };

		CVar<float> cvChromaAmount{
			"r.Random.ChromaAmount", 0.15f,
			"色ノイズの量。0 でモノクロノイズ",
			CVarRange{ 0.0f, 1.0f } };

		CVar<bool> cvEnabled{
		    "r.Random.Enabled", false,
		    "ランダムノイズを有効にする",
		    CVarRange{}, CVarFlags::NoUI };

		constexpr const char* kCVarPrefix = "r.Random";
	}

	void Random::OnCreateConstantBuffers()
	{
		UINT randomSize = (sizeof(RandomParams) + 255) & ~255;
		randomParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), randomSize);
		[[maybe_unused]] HRESULT hr = randomParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedRandomParams_));
		assert(SUCCEEDED(hr));
		UpdateConstantBuffer();

	}

	void Random::UpdateConstantBuffer()
	{
		if (!mappedRandomParams_) {
			return;
		}
		mappedRandomParams_->intensity = cvIntensity.Get();
		mappedRandomParams_->blend = cvBlend.Get();
		mappedRandomParams_->speed = cvSpeed.Get();
		mappedRandomParams_->time = accumulatedTime_;  // 実行時に累積される値
		mappedRandomParams_->grainScale = cvGrainScale.Get();
		mappedRandomParams_->luminanceInfluence = cvLuminanceInfluence.Get();
		mappedRandomParams_->chromaAmount = cvChromaAmount.Get();
	}

	void Random::PrepareFrame(const PostEffectFrameContext& ctx)
	{
		accumulatedTime_ += ctx.deltaTime * cvSpeed.Get();
		UpdateConstantBuffer();
	}

	void Random::Dispatch(
		D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
		uint32_t width,
		uint32_t height)
	{
		UpdateConstantBuffer();
		UpdateScreenSizeConstants(width, height);

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
		if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

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

		CVarUI::DrawTree(kCVarPrefix);

		UI::Separator();
		if (ImGui::Button("デフォルトに戻す")) {
			CVarUI::ResetTree(kCVarPrefix);
			accumulatedTime_ = 0.0f;
		}
		ImGui::PopID();
#endif // USE_IMGUI
	}

	CVar<bool>* Random::GetEnabledCVar() const
	{
		return &cvEnabled;
	}
}

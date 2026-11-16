#include "pch.h"
#include "Random.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#endif


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

	Random::RandomParams Random::MakeParams() const
	{
		RandomParams params{};
		params.intensity = cvIntensity.Get();
		params.blend = cvBlend.Get();
		params.speed = cvSpeed.Get();
		params.time = accumulatedTime_;  // 実行時に累積される値
		params.grainScale = cvGrainScale.Get();
		params.luminanceInfluence = cvLuminanceInfluence.Get();
		params.chromaAmount = cvChromaAmount.Get();
		return params;
	}

	void Random::PrepareFrame(const PostEffectFrameContext& ctx)
	{
		accumulatedTime_ += ctx.deltaTime * cvSpeed.Get();
	}

	void Random::Dispatch(
		D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
		uint32_t width,
		uint32_t height)
	{
		DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height, "RandomParams", MakeParams());
	}

	void Random::DrawImGui()
	{
#ifdef CORE_EDITOR
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
#endif // CORE_EDITOR
	}

	CVar<bool>* Random::GetEnabledCVar() const
	{
		return &cvEnabled;
	}
}

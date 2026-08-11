#include "pch.h"
#include "FadeEffect.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>
#include <algorithm>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvSpiralPower{
            "r.Fade.SpiralPower", 5.0f,
            "渦巻きフェードの渦の強さ",
            CVarRange{ 1.0f, 20.0f } };

        CVar<float> cvRippleFreq{
            "r.Fade.RippleFrequency", 10.0f,
            "波紋フェードの波の細かさ",
            CVarRange{ 1.0f, 30.0f } };

        CVar<float> cvGlitchIntensity{
            "r.Fade.GlitchIntensity", 0.5f,
            "グリッチフェードの乱れの強さ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvPortalSize{
            "r.Fade.PortalSize", 0.3f,
            "ポータルフェードの穴の大きさ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvColorShift{
            "r.Fade.ColorShift", 0.0f,
            "フェード中の色相シフト量",
            CVarRange{ 0.0f, 1.0f } };

        // フェードの有効/無効は SceneTransition が遷移のたびに切り替える実行時状態のため、
        // NoSave にして保存対象から外す（保存すると前回の遷移途中の状態で起動してしまう）
        CVar<bool> cvEnabled{
            "r.Fade.Enabled", false,
            "フェード効果を有効にする（通常は SceneTransition が自動で切り替える）",
            CVarRange{}, CVarFlags::NoSave | CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Fade";
    }

    void FadeEffect::OnCreateConstantBuffers()
    {
        UINT fadeSize = (sizeof(FadeParams) + 255) & ~255;
        fadeParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), fadeSize);
        HRESULT hr = fadeParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedFadeParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

    }

    void FadeEffect::UpdateConstantBuffer()
    {
        if (!mappedFadeParams_) {
            return;
        }
        mappedFadeParams_->spiralPower     = cvSpiralPower.Get();
        mappedFadeParams_->rippleFreq      = cvRippleFreq.Get();
        mappedFadeParams_->glitchIntensity = cvGlitchIntensity.Get();
        mappedFadeParams_->portalSize      = cvPortalSize.Get();
        mappedFadeParams_->colorShift      = cvColorShift.Get();
        // 遷移の進行状態は SceneTransition が制御する実行時値
        mappedFadeParams_->fadeAlpha = fadeAlpha_;
        mappedFadeParams_->fadeType  = fadeType_;
        mappedFadeParams_->time      = timeAccumulator_;
    }

    void FadeEffect::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        timeAccumulator_ += ctx.deltaTime;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetFadeAlpha(float alpha)
    {
        fadeAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
        UpdateConstantBuffer();
    }

    void FadeEffect::SetFadeType(FadeType type)
    {
        fadeType_ = static_cast<float>(type);
        UpdateConstantBuffer();
    }

    void FadeEffect::Dispatch(
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
        int outputIdx  = GetRootParamIndex("gOutput");
        int fadeIdx    = GetRootParamIndex("FadeParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (fadeIdx >= 0)    cmdList->SetComputeRootConstantBufferView(fadeIdx, fadeParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void FadeEffect::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("FadeEffect");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        // フェードタイプと進行度はシーン遷移が制御する実行時状態のため、
        // CVar ではなくここで直接編集する（保存すると黒画面で起動してしまう）
        const char* typeNames[] = { "黒フェード", "白フェード", "渦巻きフェード", "波紋フェード", "グリッチフェード", "ポータルフェード" };
        int currentType = static_cast<int>(fadeType_);
        if (ImGui::Combo("フェードタイプ", &currentType, typeNames, 6)) {
            fadeType_ = static_cast<float>(currentType);
            UpdateConstantBuffer();
        }
        if (UI::SliderFloat("フェード強度（実行時）", fadeAlpha_, 0.0f, 1.0f)) {
            UpdateConstantBuffer();
        }

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
            timeAccumulator_ = 0.0f;
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* FadeEffect::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

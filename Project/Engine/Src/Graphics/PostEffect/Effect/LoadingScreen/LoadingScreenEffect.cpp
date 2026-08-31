#include "pch.h"
#include "LoadingScreenEffect.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
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
        CVar<float> cvSpeed{
            "r.Loading.Speed", 0.75f,
            "ローディング弧が回る速さ",
            CVarRange{ 0.1f, 3.0f } };

        CVar<float> cvSpring{
            "r.Loading.Spring", 2.30f,
            "回転が行き過ぎてから戻るバネの強さ",
            CVarRange{ 0.0f, 4.0f } };

        CVar<float> cvStepTurn{
            "r.Loading.StepTurn", 0.70f,
            "1 段あたりの回転量（PI 単位）",
            CVarRange{ 0.2f, 1.5f } };

        CVar<int> cvArcCount{
            "r.Loading.ArcCount", 3,
            "弧の本数",
            CVarRange{ 1.0f, 4.0f } };

        CVar<float> cvArcLength{
            "r.Loading.ArcLength", 0.37f,
            "弧の開き角（PI 単位）",
            CVarRange{ 0.05f, 1.0f } };

        CVar<float> cvArcPulse{
            "r.Loading.ArcPulse", 0.19f,
            "弧の開き角が伸び縮みする量",
            CVarRange{ 0.0f, 0.5f } };

        CVar<float> cvThickness{
            "r.Loading.Thickness", 10.0f,
            "弧の太さ（縦 720 基準のピクセル）",
            CVarRange{ 2.0f, 24.0f } };

        CVar<float> cvRadius{
            "r.Loading.Radius", 39.0f,
            "弧の半径（縦 720 基準のピクセル）",
            CVarRange{ 10.0f, 120.0f } };

        CVar<bool> cvTipDot{
            "r.Loading.TipDot", true,
            "弧の先端に玉を付ける" };

        CVar<bool> cvTrack{
            "r.Loading.Track", true,
            "弧の軌道リングを表示する" };

        CVar<float> cvTrackAlpha{
            "r.Loading.TrackAlpha", 1.0f,
            "軌道リングの濃さ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<bool> cvHueCycle{
            "r.Loading.HueCycle", true,
            "弧の色相を巡回させる（無効時は ArcColor0〜2 を使う）" };

        CVar<float> cvCenterX{
            "r.Loading.CenterX", 0.5f,
            "表示位置（画面幅に対する比率）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvCenterY{
            "r.Loading.CenterY", 0.5f,
            "表示位置（画面高さに対する比率）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<Vector4> cvArcColor0{
            "r.Loading.ArcColor0", Vector4{ 1.0f, 0.365f, 0.561f, 1.0f },
            "1 本目の弧の色（HueCycle 無効時）" };

        CVar<Vector4> cvArcColor1{
            "r.Loading.ArcColor1", Vector4{ 0.208f, 0.816f, 0.847f, 1.0f },
            "2 本目の弧の色（HueCycle 無効時）" };

        CVar<Vector4> cvArcColor2{
            "r.Loading.ArcColor2", Vector4{ 1.0f, 0.788f, 0.235f, 1.0f },
            "3 本目の弧の色（HueCycle 無効時）" };

        CVar<Vector4> cvTrackColor{
            "r.Loading.TrackColor", Vector4{ 0.141f, 0.157f, 0.200f, 1.0f },
            "軌道リングの色" };

        // 表示強度は SceneTransition が遷移のたびに切り替える実行時状態のため保存しない
        CVar<bool> cvEnabled{
            "r.Loading.Enabled", false,
            "ローディング画面を有効にする（通常は SceneTransition が自動で切り替える）",
            CVarRange{}, CVarFlags::NoSave | CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Loading";

        // 1 フレームで進める時間の上限
        constexpr float kMaxDeltaSeconds = 1.0f / 30.0f;
    }

    void LoadingScreenEffect::OnCreateConstantBuffers()
    {
        UINT paramsSize = (sizeof(LoadingParams) + 255) & ~255;
        loadingParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), paramsSize);
        [[maybe_unused]] HRESULT hr = loadingParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedLoadingParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }

    void LoadingScreenEffect::UpdateConstantBuffer()
    {
        if (!mappedLoadingParams_) {
            return;
        }
        mappedLoadingParams_->speed        = cvSpeed.Get();
        mappedLoadingParams_->spring       = cvSpring.Get();
        mappedLoadingParams_->stepTurn     = cvStepTurn.Get();
        mappedLoadingParams_->arcCount     = static_cast<float>(cvArcCount.Get());
        mappedLoadingParams_->arcLength    = cvArcLength.Get();
        mappedLoadingParams_->arcPulse     = cvArcPulse.Get();
        mappedLoadingParams_->thickness    = cvThickness.Get();
        mappedLoadingParams_->radius       = cvRadius.Get();
        mappedLoadingParams_->tipDot       = cvTipDot.Get() ? 1.0f : 0.0f;
        mappedLoadingParams_->trackEnabled = cvTrack.Get() ? 1.0f : 0.0f;
        mappedLoadingParams_->trackAlpha   = cvTrackAlpha.Get();
        mappedLoadingParams_->hueCycle     = cvHueCycle.Get() ? 1.0f : 0.0f;
        mappedLoadingParams_->centerX      = cvCenterX.Get();
        mappedLoadingParams_->centerY      = cvCenterY.Get();
        mappedLoadingParams_->arcColor0    = cvArcColor0.Get();
        mappedLoadingParams_->arcColor1    = cvArcColor1.Get();
        mappedLoadingParams_->arcColor2    = cvArcColor2.Get();
        mappedLoadingParams_->trackColor   = cvTrackColor.Get();
        // 表示強度と経過時間はシーン遷移が制御する実行時値
        mappedLoadingParams_->screenAlpha  = screenAlpha_;
        mappedLoadingParams_->time         = timeAccumulator_;
        mappedLoadingParams_->progress     = progress_;
        mappedLoadingParams_->gaugeAlpha   = gaugeAlpha_;
    }

    // デルタタイムに上限を掛けて積算する
    void LoadingScreenEffect::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        timeAccumulator_ += std::min(ctx.deltaTime, kMaxDeltaSeconds);
        UpdateConstantBuffer();
    }

    void LoadingScreenEffect::SetScreenAlpha(float alpha)
    {
        screenAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
        UpdateConstantBuffer();
    }

    void LoadingScreenEffect::SetProgress(float progress)
    {
        progress_ = std::clamp(progress, 0.0f, 1.0f);
        UpdateConstantBuffer();
    }

    void LoadingScreenEffect::SetGaugeAlpha(float alpha)
    {
        gaugeAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
        UpdateConstantBuffer();
    }

    void LoadingScreenEffect::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int paramsIdx  = GetRootParamIndex("LoadingParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, loadingParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void LoadingScreenEffect::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("LoadingScreenEffect");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        // 表示強度はシーン遷移が制御する実行時状態のため、CVar ではなくここで直接編集する
        if (UI::SliderFloat("表示強度（実行時）", screenAlpha_, 0.0f, 1.0f)) {
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

    CVar<bool>* LoadingScreenEffect::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

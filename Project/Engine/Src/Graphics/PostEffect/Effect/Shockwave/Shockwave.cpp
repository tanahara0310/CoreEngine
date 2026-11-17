#include "pch.h"
#include "Shockwave.h"
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
        CVar<float> cvStrength{
            "r.Shockwave.Strength", 0.1f,
            "衝撃波による歪みの強さ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvThickness{
            "r.Shockwave.Thickness", 0.1f,
            "波のリングの厚み",
            CVarRange{ 0.01f, 0.5f } };

        CVar<float> cvSpeed{
            "r.Shockwave.Speed", 1.0f,
            "波が広がる速さ",
            CVarRange{ 0.1f, 5.0f } };

        CVar<bool> cvEnabled{
            "r.Shockwave.Enabled", false,
            "ショックウェーブを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Shockwave";
    }

    Shockwave::ShockwaveParams Shockwave::MakeParams() const
    {
        ShockwaveParams params{};
        params.strength  = cvStrength.Get();
        params.thickness = cvThickness.Get();
        params.speed     = cvSpeed.Get();
        // 発動状態は実行時の値
        params.center[0] = centerX_;
        params.center[1] = centerY_;
        params.time      = time_;
        return params;
    }

    void Shockwave::StartShockwave(float centerX, float centerY)
    {
        centerX_  = centerX;
        centerY_  = centerY;
        time_     = 0.0f;
        isActive_ = true;
    }

    void Shockwave::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        if (!isActive_) { return; }

        time_ += ctx.deltaTime * cvSpeed.Get();
        if (time_ >= maxRadius_) {
            isActive_ = false;
            time_     = 0.0f;
        }
    }

    void Shockwave::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height, "ShockwaveParams", MakeParams());
    }

    void Shockwave::DrawImGui()
    {
#ifdef CORE_EDITOR
        BeginEffectImGui("ShockwaveParams");
        ImGui::Text("アクティブ: %s", isActive_ ? "true" : "false");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("ショックウェーブ発動")) {
            StartShockwave(0.5f, 0.5f);
        }
        ImGui::SameLine();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        EndEffectImGui();
#endif // CORE_EDITOR
    }

    CVar<bool>* Shockwave::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

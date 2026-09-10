#include "pch.h"
#include "RockBreakShakeSettingsComponent.h"

#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakePresets.h"

namespace GameComponents
{
    CoreEngine::CVar<float> RockBreakShakeSettingsComponent::Strength{
        "Game.CameraShake.RockBreak.Strength",
        1.0f,
        "岩を壊した瞬間のカメラシェイク強度（0で無効、1で標準）",
        CoreEngine::CVarRange{ 0.0f, 3.0f } };

    CoreEngine::CVar<float> RockBreakShakeSettingsComponent::Duration{
        "Game.CameraShake.RockBreak.Duration",
        0.35f,
        "岩を壊した瞬間のカメラシェイクの長さ（秒）",
        CoreEngine::CVarRange{ 0.05f, 1.5f } };

    CoreEngine::CVar<float> RockBreakShakeSettingsComponent::Frequency{
        "Game.CameraShake.RockBreak.Frequency",
        22.0f,
        "岩を壊した瞬間のカメラシェイクの細かさ（Hz）",
        CoreEngine::CVarRange{ 1.0f, 60.0f } };

    void RockBreakShakeSettingsComponent::PlayRockBreak()
    {
        const float strength = Strength.Get();

        // シーン切り替え中は揺れの受け手が居ない。鳴らさずに抜ける。
        if (strength <= 0.0f || !CoreEngine::CameraShake::IsAvailable()) {
            return;
        }

        // 重い一撃のプリセットを土台にして、強さ・長さ・細かさだけ CVar で差し替える。
        // 波形と減衰カーブはプリセットのまま（岩が砕ける手応えはここで決まっている）。
        CoreEngine::CameraShakeParams params = CoreEngine::CameraShakePresets::HeavyHit();
        params.positionAmplitude = params.positionAmplitude * strength;
        params.rotationAmplitude = params.rotationAmplitude * strength;
        params.duration = Duration.Get();
        params.frequency = Frequency.Get();

        CoreEngine::CameraShake::Play(params);
    }
}

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"

namespace GameComponents
{
    bool RockBreakShakeSettingsComponent::DrawInspector()
    {
        constexpr const char* kRockBreakCVarPrefix = "Game.CameraShake.RockBreak";
        const bool changed = CoreEngine::CVarUI::DrawTree(kRockBreakCVarPrefix);

        CoreEngine::UI::Separator();
        CoreEngine::UI::Hint(
            "サルが岩を壊し切った瞬間に鳴ります。値はCVarとして保存され、"
            "次に岩を壊したときから反映されます。");
        return changed;
    }
}
#endif

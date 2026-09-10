#include "pch.h"
#include "TitleCameraShakeSettingsComponent.h"

namespace GameComponents
{
    CoreEngine::CVar<float> TitleCameraShakeSettingsComponent::ShakeStrength{
        "Title.CameraShake.Strength",
        1.0f,
        "タイトルモデルのバウンド時カメラシェイク強度（0で無効、1で標準）",
        CoreEngine::CVarRange{ 0.0f, 2.0f } };
}

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"

namespace GameComponents
{
    bool TitleCameraShakeSettingsComponent::DrawInspector()
    {
        constexpr const char* kCameraShakeCVarPrefix = "Title.CameraShake";
        const bool changed = CoreEngine::CVarUI::DrawTree(
            kCameraShakeCVarPrefix);

        CoreEngine::UI::Separator();
        CoreEngine::UI::Hint(
            "値はCVarとして保存されます。タイトルシーンを再読み込みすると"
            "バウンド時のカメラシェイクへ反映されます。");
        return changed;
    }
}
#endif

#include "Invert.h"
#include "Utility/Debug/ImGui/ImguiManager.h"


namespace CoreEngine
{
void Invert::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushID("Invert");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("画像内のすべての色を反転します（ネガティブ効果）");
    ImGui::Text("計算式: output.rgb = 1.0 - input.rgb");
    UI::Separator();
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効です");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "画像に色反転を適用中");
    }
    
    ImGui::PopID();
#endif // USE_IMGUI
}
}

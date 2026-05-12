#include "ToneMapping.h"
#include "Utility/Debug/ImGui/ImguiManager.h"


namespace CoreEngine
{
void ToneMapping::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushID("ToneMapping");

    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("ACES トーンマッピング（HDR→LDR変換）");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "このエフェクトは常に有効です。");
    UI::Separator();

    ImGui::PopID();
#endif // USE_IMGUI
}
}

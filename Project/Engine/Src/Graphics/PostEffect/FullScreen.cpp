#include "FullScreen.h"
#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
const std::wstring& FullScreen::GetPixelShaderPath() const
{
    static const std::wstring path = L"Engine/Assets/Shaders/PostProcess/FullScreen.PS.hlsl";
    return path;
}

void FullScreen::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushID("FullScreenParams");
    
    ImGui::Text("Status: Always Enabled (Display Effect)");
    ImGui::Text("FullScreen Effect");
    ImGui::Text("Used for final display to back buffer.");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "This effect cannot be disabled.");
    UI::Separator();
    
    ImGui::PopID();
#endif // USE_IMGUI
}
}

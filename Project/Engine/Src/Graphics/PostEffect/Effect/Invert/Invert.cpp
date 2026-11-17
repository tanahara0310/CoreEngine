#include "pch.h"
#include "Invert.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.Invert.Enabled", false,
            "色反転を有効にする",
            CVarRange{}, CVarFlags::NoUI };
    }

    void Invert::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height);
    }
void Invert::DrawImGui()
{
#ifdef CORE_EDITOR
    BeginEffectImGui("Invert");
    ImGui::Text("画像内のすべての色を反転します（ネガティブ効果）");
    ImGui::Text("計算式: output.rgb = 1.0 - input.rgb");
    UI::Separator();
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効です");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "画像に色反転を適用中");
    }
    
    EndEffectImGui();
#endif // CORE_EDITOR
}

    CVar<bool>* Invert::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

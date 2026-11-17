#include "pch.h"
#include "GrayScale.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.GrayScale.Enabled", false,
            "グレースケール変換を有効にする",
            CVarRange{}, CVarFlags::NoUI };
    }

    void GrayScale::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height);
    }

    void GrayScale::DrawImGui()
    {
#ifdef CORE_EDITOR
        BeginEffectImGui("GrayScale");
        ImGui::Text("画像をグレースケールに変換します");
        UI::Separator();
        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効です");
        }
        EndEffectImGui();
#endif // CORE_EDITOR
    }

    CVar<bool>* GrayScale::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

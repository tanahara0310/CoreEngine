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
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx = GetRootParamIndex("gOutput");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)      cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)       cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (screenParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(screenParamsIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void GrayScale::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("GrayScale");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("画像をグレースケールに変換します");
        UI::Separator();
        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効です");
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* GrayScale::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

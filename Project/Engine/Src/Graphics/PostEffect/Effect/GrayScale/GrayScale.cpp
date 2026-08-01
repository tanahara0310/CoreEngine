#include "pch.h"
#include "GrayScale.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
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

    void GrayScale::OnCreateConstantBuffers()
    {
        UINT size = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), size);
        [[maybe_unused]] HRESULT hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void GrayScale::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void GrayScale::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx = GetRootParamIndex("gOutput");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)      cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)       cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (screenParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

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

#include "pch.h"
#include "Invert.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void Invert::OnCreateConstantBuffers()
    {
        UINT size = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), size);
        [[maybe_unused]] HRESULT hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Invert::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Invert::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx      = GetRootParamIndex("gTexture");
        int outputIdx       = GetRootParamIndex("gOutput");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)      cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)       cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (screenParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }
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

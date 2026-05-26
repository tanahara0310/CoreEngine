#include "pch.h"
#include "Vignette.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void Vignette::OnCreateConstantBuffers()
    {
        UINT vignetteSize = (sizeof(VignetteParams) + 255) & ~255;
        vignetteParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), vignetteSize);
        HRESULT hr = vignetteParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedVignetteParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Vignette::UpdateConstantBuffer()
    {
        if (mappedVignetteParams_) { *mappedVignetteParams_ = params_; }
    }

    void Vignette::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Vignette::SetParams(const VignetteParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Vignette::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx       = GetRootParamIndex("gTexture");
        int outputIdx        = GetRootParamIndex("gOutput");
        int vignetteParmsIdx = GetRootParamIndex("VignetteParams");
        int screenParamsIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)       cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)        cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (vignetteParmsIdx >= 0) cmdList->SetComputeRootConstantBufferView(vignetteParmsIdx, vignetteParamsCB_->GetGPUVirtualAddress());
        if (screenParamsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Vignette::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("VignetteParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("強度", params_.intensity, 0.0f, 2.0f);
            changed |= UI::SliderFloat("スムースネス", params_.smoothness, 0.1f, 2.0f);
            changed |= UI::SliderFloat("サイズ", params_.size, 1.0f, 50.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = VignetteParams{};
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}

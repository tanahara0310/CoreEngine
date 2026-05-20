#include "ChromaticAberration.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void ChromaticAberration::OnCreateConstantBuffers()
    {
        UINT caSize = (sizeof(ChromaticAberrationParams) + 255) & ~255;
        caParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), caSize);
        HRESULT hr = caParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedCAParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void ChromaticAberration::UpdateConstantBuffer()
    {
        if (mappedCAParams_) { *mappedCAParams_ = params_; }
    }

    void ChromaticAberration::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void ChromaticAberration::SetParams(const ChromaticAberrationParams& newParams)
    {
        params_ = newParams;
        UpdateConstantBuffer();
    }

    void ChromaticAberration::ApplyPreset(int presetIndex)
    {
        ChromaticAberrationParams preset;
        switch (presetIndex) {
        case 1: // 強め
            preset.intensity = 8.0f;
            preset.radialFactor = 1.5f;
            break;
        case 2: // 弱め
            preset.intensity = 1.5f;
            preset.radialFactor = 0.5f;
            break;
        default:
            break;
        }
        SetParams(preset);
    }

    void ChromaticAberration::Dispatch(
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
        int outputIdx  = GetRootParamIndex("gOutput");
        int caIdx      = GetRootParamIndex("ChromaticAberrationParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (caIdx >= 0)      cmdList->SetComputeRootConstantBufferView(caIdx, caParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void ChromaticAberration::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ChromaticAberration");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("強度", params_.intensity, 0.0f, 20.0f);
            changed |= UI::SliderFloat("放射状強度", params_.radialFactor, 0.0f, 3.0f);
            changed |= UI::SliderFloat("中心X", params_.centerX, 0.0f, 1.0f);
            changed |= UI::SliderFloat("中心Y", params_.centerY, 0.0f, 1.0f);
            changed |= UI::SliderFloat("歪みスケール", params_.distortionScale, 0.0f, 5.0f);
            changed |= UI::SliderFloat("フォールオフ", params_.falloff, 0.1f, 5.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = ChromaticAberrationParams{};
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}

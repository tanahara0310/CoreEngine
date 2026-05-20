#include "RadialBlur.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void RadialBlur::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;
        InitializeComputeCore();
    }

    void RadialBlur::OnCreateConstantBuffers()
    {
        UINT rbSize = (sizeof(RadialBlurParams) + 255) & ~255;
        radialBlurParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), rbSize);
        HRESULT hr = radialBlurParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedRadialBlurParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void RadialBlur::UpdateConstantBuffer()
    {
        if (mappedRadialBlurParams_) { *mappedRadialBlurParams_ = params_; }
    }

    void RadialBlur::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void RadialBlur::SetParams(const RadialBlurParams& newParams)
    {
        params_ = newParams;
        UpdateConstantBuffer();
    }

    void RadialBlur::Dispatch(
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
        int rbIdx      = GetRootParamIndex("RadialBlurParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (rbIdx >= 0)      cmdList->SetComputeRootConstantBufferView(rbIdx, radialBlurParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void RadialBlur::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("RadialBlurParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("強度", params_.intensity, 0.0f, 2.0f);
            changed |= UI::SliderFloat("サンプル数", params_.sampleCount, 4.0f, 16.0f);
            changed |= UI::SliderFloat("中心X", params_.centerX, 0.0f, 1.0f);
            changed |= UI::SliderFloat("中心Y", params_.centerY, 0.0f, 1.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = RadialBlurParams{};
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}

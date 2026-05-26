#include "pch.h"
#include "Bloom.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void Bloom::OnCreateConstantBuffers()
    {
        UINT bloomSize = (sizeof(BloomParams) + 255) & ~255;
        bloomParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bloomSize);
        HRESULT hr = bloomParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedBloomParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Bloom::UpdateConstantBuffer()
    {
        if (mappedBloomParams_) { *mappedBloomParams_ = params_; }
    }

    void Bloom::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Bloom::SetParams(const BloomParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Bloom::Dispatch(
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
        int bloomIdx   = GetRootParamIndex("BloomParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (bloomIdx >= 0)   cmdList->SetComputeRootConstantBufferView(bloomIdx, bloomParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Bloom::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("BloomParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("輝度閾値", params_.threshold, 0.0f, 2.0f);
            changed |= UI::SliderFloat("強度", params_.intensity, 0.0f, 3.0f);
            changed |= UI::SliderFloat("ブラー半径", params_.blurRadius, 0.5f, 5.0f);
            changed |= UI::SliderFloat("ソフトニー", params_.softKnee, 0.0f, 1.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = BloomParams{};
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}

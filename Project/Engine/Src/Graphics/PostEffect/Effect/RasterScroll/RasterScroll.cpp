#include "pch.h"
#include "RasterScroll.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void RasterScroll::OnCreateConstantBuffers()
    {
        UINT rsSize = (sizeof(RasterScrollParams) + 255) & ~255;
        rasterScrollParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), rsSize);
        HRESULT hr = rasterScrollParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedRasterScrollParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void RasterScroll::UpdateConstantBuffer()
    {
        if (mappedRasterScrollParams_) { *mappedRasterScrollParams_ = params_; }
    }

    void RasterScroll::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void RasterScroll::SetParams(const RasterScrollParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void RasterScroll::Update(float deltaTime)
    {
        accumulatedTime_  += deltaTime;
        params_.time       = accumulatedTime_;
        params_.lineOffset = accumulatedTime_ * params_.scrollSpeed;
        UpdateConstantBuffer();
    }

    void RasterScroll::Dispatch(
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
        int rsIdx      = GetRootParamIndex("RasterScrollParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (rsIdx >= 0)      cmdList->SetComputeRootConstantBufferView(rsIdx, rasterScrollParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void RasterScroll::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("RasterScrollParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("スクロール速度", params_.scrollSpeed, 0.0f, 10.0f);
            changed |= UI::SliderFloat("ライン高さ", params_.lineHeight, 1.0f, 20.0f);
            changed |= UI::SliderFloat("振幅", params_.amplitude, 0.0f, 0.2f);
            changed |= UI::SliderFloat("周波数", params_.frequency, 0.1f, 5.0f);
            changed |= UI::SliderFloat("歪み強度", params_.distortionStrength, 0.0f, 3.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = RasterScrollParams{};
            accumulatedTime_ = 0.0f;
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}

#include "pch.h"
#include "Shockwave.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void Shockwave::OnCreateConstantBuffers()
    {
        UINT swSize = (sizeof(ShockwaveParams) + 255) & ~255;
        shockwaveParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), swSize);
        HRESULT hr = shockwaveParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedShockwaveParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Shockwave::UpdateConstantBuffer()
    {
        if (mappedShockwaveParams_) { *mappedShockwaveParams_ = params_; }
    }

    void Shockwave::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Shockwave::SetParams(const ShockwaveParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Shockwave::StartShockwave(float centerX, float centerY)
    {
        params_.center[0] = centerX;
        params_.center[1] = centerY;
        params_.time      = 0.0f;
        isActive_         = true;
        UpdateConstantBuffer();
    }

    void Shockwave::Update(float deltaTime)
    {
        if (!isActive_) { return; }

        params_.time += deltaTime * params_.speed;
        if (params_.time >= maxRadius_) {
            isActive_    = false;
            params_.time = 0.0f;
        }
        UpdateConstantBuffer();
    }

    void Shockwave::Dispatch(
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
        int swIdx      = GetRootParamIndex("ShockwaveParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (swIdx >= 0)      cmdList->SetComputeRootConstantBufferView(swIdx, shockwaveParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Shockwave::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ShockwaveParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("アクティブ: %s", isActive_ ? "true" : "false");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("強度", params_.strength, 0.0f, 1.0f);
            changed |= UI::SliderFloat("波の厚さ", params_.thickness, 0.01f, 0.5f);
            changed |= UI::SliderFloat("速度", params_.speed, 0.1f, 5.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("ショックウェーブ発動")) {
            StartShockwave(0.5f, 0.5f);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}

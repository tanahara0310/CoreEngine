#include "pch.h"
#include "Bloom.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvThreshold{
            "r.Bloom.Threshold", 0.8f,
            "ブルームを発生させる輝度の閾値。低いほど広い範囲が光る",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvIntensity{
            "r.Bloom.Intensity", 1.0f,
            "ブルームの強度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvBlurRadius{
            "r.Bloom.BlurRadius", 2.0f,
            "にじみの広がり半径",
            CVarRange{ 0.5f, 5.0f } };

        CVar<float> cvSoftKnee{
            "r.Bloom.SoftKnee", 0.5f,
            "閾値付近のなめらかさ。0 で硬い切り替わり",
            CVarRange{ 0.0f, 1.0f } };

        CVar<bool> cvEnabled{
            "r.Bloom.Enabled", false,
            "ブルームを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Bloom";
    }

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
        if (!mappedBloomParams_) {
            return;
        }
        mappedBloomParams_->threshold  = cvThreshold.Get();
        mappedBloomParams_->intensity  = cvIntensity.Get();
        mappedBloomParams_->blurRadius = cvBlurRadius.Get();
        mappedBloomParams_->softKnee   = cvSoftKnee.Get();
    }

    void Bloom::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Bloom::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
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

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* Bloom::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

#include "pch.h"
#include "RadialBlur.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvIntensity{
            "r.RadialBlur.Intensity", 0.5f,
            "放射状ブラーの強度",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvSampleCount{
            "r.RadialBlur.SampleCount", 8.0f,
            "サンプル数。多いほど滑らかだが重い",
            CVarRange{ 4.0f, 16.0f } };

        CVar<float> cvCenterX{
            "r.RadialBlur.CenterX", 0.5f,
            "ブラーの中心 X（画面比 0-1）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvCenterY{
            "r.RadialBlur.CenterY", 0.5f,
            "ブラーの中心 Y（画面比 0-1）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<bool> cvEnabled{
            "r.RadialBlur.Enabled", false,
            "ラジアルブラーを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.RadialBlur";
    }

    void RadialBlur::OnCreateConstantBuffers()
    {
        UINT rbSize = (sizeof(RadialBlurParams) + 255) & ~255;
        radialBlurParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), rbSize);
        [[maybe_unused]] HRESULT hr = radialBlurParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedRadialBlurParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

    }

    void RadialBlur::UpdateConstantBuffer()
    {
        if (!mappedRadialBlurParams_) {
            return;
        }
        mappedRadialBlurParams_->intensity   = cvIntensity.Get();
        mappedRadialBlurParams_->sampleCount = cvSampleCount.Get();
        mappedRadialBlurParams_->centerX     = cvCenterX.Get();
        mappedRadialBlurParams_->centerY     = cvCenterY.Get();
    }

    void RadialBlur::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int rbIdx      = GetRootParamIndex("RadialBlurParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (rbIdx >= 0)      cmdList->SetComputeRootConstantBufferView(rbIdx, radialBlurParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

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

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* RadialBlur::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

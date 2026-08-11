#include "pch.h"
#include "ColorGrading.h"
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
        CVar<float> cvHue{
            "r.ColorGrading.Hue", 0.0f,
            "色相の回転量",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> cvSaturation{
            "r.ColorGrading.Saturation", 1.0f,
            "彩度。0 でモノクロ",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvValue{
            "r.ColorGrading.Value", 1.0f,
            "明度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvContrast{
            "r.ColorGrading.Contrast", 1.0f,
            "コントラスト",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvGamma{
            "r.ColorGrading.Gamma", 1.0f,
            "ガンマ補正",
            CVarRange{ 0.1f, 3.0f } };

        CVar<float> cvTemperature{
            "r.ColorGrading.Temperature", 0.0f,
            "色温度。正で暖色、負で寒色",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> cvTint{
            "r.ColorGrading.Tint", 0.0f,
            "ティント（マゼンタ⇔グリーン）",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> cvExposure{
            "r.ColorGrading.Exposure", 0.0f,
            "露出調整",
            CVarRange{ -3.0f, 3.0f } };

        CVar<Vector3> cvShadowLift{
            "r.ColorGrading.ShadowLift", Vector3{ 0.0f, 0.0f, 0.0f },
            "暗部の持ち上げ量（RGB 個別）",
            CVarRange{ -1.0f, 1.0f } };

        CVar<Vector3> cvMidtoneGamma{
            "r.ColorGrading.MidtoneGamma", Vector3{ 1.0f, 1.0f, 1.0f },
            "中間調のガンマ（RGB 個別）",
            CVarRange{ 0.1f, 3.0f } };

        CVar<Vector3> cvHighlightGain{
            "r.ColorGrading.HighlightGain", Vector3{ 1.0f, 1.0f, 1.0f },
            "明部のゲイン（RGB 個別）",
            CVarRange{ 0.0f, 3.0f } };

        CVar<bool> cvEnabled{
            "r.ColorGrading.Enabled", false,
            "カラーグレーディングを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.ColorGrading";
    }

    void ColorGrading::OnCreateConstantBuffers()
    {
        UINT cgSize = (sizeof(ColorGradingParams) + 255) & ~255;
        colorGradingParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), cgSize);
        HRESULT hr = colorGradingParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedColorGradingParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

    }

    void ColorGrading::UpdateConstantBuffer()
    {
        if (!mappedColorGradingParams_) {
            return;
        }
        mappedColorGradingParams_->hue         = cvHue.Get();
        mappedColorGradingParams_->saturation  = cvSaturation.Get();
        mappedColorGradingParams_->value       = cvValue.Get();
        mappedColorGradingParams_->contrast    = cvContrast.Get();
        mappedColorGradingParams_->gamma       = cvGamma.Get();
        mappedColorGradingParams_->temperature = cvTemperature.Get();
        mappedColorGradingParams_->tint        = cvTint.Get();
        mappedColorGradingParams_->exposure    = cvExposure.Get();

        const Vector3& shadowLift = cvShadowLift.Get();
        mappedColorGradingParams_->shadowLift[0] = shadowLift.x;
        mappedColorGradingParams_->shadowLift[1] = shadowLift.y;
        mappedColorGradingParams_->shadowLift[2] = shadowLift.z;

        const Vector3& midtoneGamma = cvMidtoneGamma.Get();
        mappedColorGradingParams_->midtoneGamma[0] = midtoneGamma.x;
        mappedColorGradingParams_->midtoneGamma[1] = midtoneGamma.y;
        mappedColorGradingParams_->midtoneGamma[2] = midtoneGamma.z;

        const Vector3& highlightGain = cvHighlightGain.Get();
        mappedColorGradingParams_->highlightGain[0] = highlightGain.x;
        mappedColorGradingParams_->highlightGain[1] = highlightGain.y;
        mappedColorGradingParams_->highlightGain[2] = highlightGain.z;
    }

    void ColorGrading::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int cgIdx      = GetRootParamIndex("ColorGradingParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (cgIdx >= 0)      cmdList->SetComputeRootConstantBufferView(cgIdx, colorGradingParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void ColorGrading::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ColorGrading");
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

    CVar<bool>* ColorGrading::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

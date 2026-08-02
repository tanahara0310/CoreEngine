#include "pch.h"
#include "ChromaticAberration.h"
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
        CVar<float> cvIntensity{
            "r.ChromaticAberration.Intensity", 3.0f,
            "色ずれの強度",
            CVarRange{ 0.0f, 20.0f } };

        CVar<float> cvRadialFactor{
            "r.ChromaticAberration.RadialFactor", 1.0f,
            "放射方向の強度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvCenterX{
            "r.ChromaticAberration.CenterX", 0.5f,
            "効果の中心 X（画面比 0-1）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvCenterY{
            "r.ChromaticAberration.CenterY", 0.5f,
            "効果の中心 Y（画面比 0-1）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvDistortionScale{
            "r.ChromaticAberration.DistortionScale", 1.0f,
            "歪み量のスケール",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> cvFalloff{
            "r.ChromaticAberration.Falloff", 1.5f,
            "画面端へ向かう強度の減衰カーブ",
            CVarRange{ 0.1f, 5.0f } };

        CVar<bool> cvEnabled{
            "r.ChromaticAberration.Enabled", false,
            "色収差を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.ChromaticAberration";
    }

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
        if (!mappedCAParams_) {
            return;
        }
        mappedCAParams_->intensity       = cvIntensity.Get();
        mappedCAParams_->radialFactor    = cvRadialFactor.Get();
        mappedCAParams_->centerX         = cvCenterX.Get();
        mappedCAParams_->centerY         = cvCenterY.Get();
        mappedCAParams_->distortionScale = cvDistortionScale.Get();
        mappedCAParams_->falloff         = cvFalloff.Get();
        mappedCAParams_->samples         = 1.0f;  // 未使用フィールド
        mappedCAParams_->padding         = 0.0f;
    }

    void ChromaticAberration::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void ChromaticAberration::Dispatch(
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

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* ChromaticAberration::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

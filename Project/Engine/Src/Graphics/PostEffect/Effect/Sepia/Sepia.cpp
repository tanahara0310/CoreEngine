#include "pch.h"
#include "Sepia.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#endif


namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.Sepia.Enabled", false,
            "セピア調変換を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvIntensity{
            "r.Sepia.Intensity", 1.0f,
            "セピア効果の強度。0 で元の色",
            CVarRange{ 0.0f, 2.0f } };

        CVar<Vector3> cvTone{
            "r.Sepia.Tone", Vector3{ 1.0f, 0.8f, 0.6f },
            "セピアの色味（RGB 個別の倍率）",
            CVarRange{ 0.5f, 1.5f } };

        constexpr const char* kCVarPrefix = "r.Sepia";
    }

    Sepia::SepiaParams Sepia::MakeParams() const
    {
        SepiaParams params{};
        params.intensity = cvIntensity.Get();

        const Vector3& tone = cvTone.Get();
        params.toneRed   = tone.x;
        params.toneGreen = tone.y;
        params.toneBlue  = tone.z;
        return params;
    }

    void Sepia::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx      = GetRootParamIndex("gTexture");
        int outputIdx       = GetRootParamIndex("gOutput");
        int sepiaParamsIdx  = GetRootParamIndex("SepiaParams");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)      cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)       cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (sepiaParamsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(sepiaParamsIdx, UploadConstants(MakeParams()));
        if (screenParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(screenParamsIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Sepia::DrawImGui()
    {
#ifdef CORE_EDITOR
        ImGui::PushID("SepiaParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }
        ImGui::PopID();
#endif // CORE_EDITOR
    }

    CVar<bool>* Sepia::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

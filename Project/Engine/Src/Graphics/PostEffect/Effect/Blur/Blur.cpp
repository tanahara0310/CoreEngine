#include "pch.h"
#include "Blur.h"
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
        CVar<float> cvIntensity{
            "r.Blur.Intensity", 1.0f,
            "ブラーの強度",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> cvKernelSize{
            "r.Blur.KernelSize", 1.0f,
            "サンプリング範囲の広さ。大きいほど広くぼける",
            CVarRange{ 0.5f, 3.0f } };

        CVar<bool> cvEnabled{
            "r.Blur.Enabled", false,
            "ガウシアンブラーを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Blur";
    }

    Blur::BlurParams Blur::MakeParams() const
    {
        BlurParams params{};
        params.intensity  = cvIntensity.Get();
        params.kernelSize = cvKernelSize.Get();
        return params;
    }

    void Blur::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height, "BlurParams", MakeParams());
    }

    void Blur::DrawImGui()
    {
#ifdef CORE_EDITOR
        ImGui::PushID("BlurParams");

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

    CVar<bool>* Blur::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

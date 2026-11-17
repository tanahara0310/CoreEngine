#include "pch.h"
#include "RadialBlur.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"


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

    RadialBlur::RadialBlurParams RadialBlur::MakeParams() const
    {
        RadialBlurParams params{};
        params.intensity   = cvIntensity.Get();
        params.sampleCount = cvSampleCount.Get();
        params.centerX     = cvCenterX.Get();
        params.centerY     = cvCenterY.Get();
        return params;
    }

    void RadialBlur::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height, "RadialBlurParams", MakeParams());
    }

    void RadialBlur::DrawImGui()
    {
#ifdef CORE_EDITOR
        DrawCVarEffectImGui("RadialBlurParams", kCVarPrefix);
#endif // CORE_EDITOR
    }

    CVar<bool>* RadialBlur::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

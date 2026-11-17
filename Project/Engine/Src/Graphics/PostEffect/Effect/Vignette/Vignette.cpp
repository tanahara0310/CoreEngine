#include "pch.h"
#include "Vignette.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"


namespace CoreEngine
{
    namespace
    {
        // ヴィネットの調整パラメータ。ここで 1 行定義するだけで、
        // ImGui のスライダー・エディタ設定への自動保存・コンソールからの操作がすべて有効になる
        CVar<float> cvIntensity{
            "r.Vignette.Intensity", 0.8f,
            "ヴィネットの強さ。0 で無効、大きいほど四隅が暗くなる",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvSmoothness{
            "r.Vignette.Smoothness", 0.8f,
            "明暗の境界のなめらかさ。小さいほど境界がはっきりする",
            CVarRange{ 0.1f, 2.0f } };

        CVar<float> cvSize{
            "r.Vignette.Size", 16.0f,
            "効果のかかり始める半径。大きいほど画面中央の明るい領域が広がる",
            CVarRange{ 1.0f, 50.0f } };

        CVar<bool> cvEnabled{
            "r.Vignette.Enabled", false,
            "ヴィネットを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Vignette";
    }

    Vignette::VignetteParams Vignette::GetParams() const
    {
        VignetteParams params;
        params.intensity  = cvIntensity.Get();
        params.smoothness = cvSmoothness.Get();
        params.size       = cvSize.Get();
        return params;
    }

    void Vignette::SetParams(const VignetteParams& params)
    {
        cvIntensity.Set(params.intensity);
        cvSmoothness.Set(params.smoothness);
        cvSize.Set(params.size);
    }

    void Vignette::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height, "VignetteParams", GetParams());
    }

    void Vignette::DrawImGui()
    {
#ifdef CORE_EDITOR
        // パラメータ UI は CVar から自動生成される。
        // 項目を増やすときは Vignette.cpp 冒頭に CVar を 1 行足すだけでよい
        DrawCVarEffectImGui("VignetteParams", kCVarPrefix);
#endif // CORE_EDITOR
    }

    CVar<bool>* Vignette::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

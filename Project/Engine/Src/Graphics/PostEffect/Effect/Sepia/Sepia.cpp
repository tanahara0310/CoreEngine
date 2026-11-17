#include "pch.h"
#include "Sepia.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"


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
        DispatchScreenPass(inputSrvHandle, outputUavHandle, width, height, "SepiaParams", MakeParams());
    }

    void Sepia::DrawImGui()
    {
#ifdef CORE_EDITOR
        DrawCVarEffectImGui("SepiaParams", kCVarPrefix, true);
#endif // CORE_EDITOR
    }

    CVar<bool>* Sepia::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

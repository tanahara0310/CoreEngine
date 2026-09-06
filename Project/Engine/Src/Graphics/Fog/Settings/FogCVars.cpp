#include "pch.h"
#include "Graphics/Fog/Settings/FogCVars.h"

namespace CoreEngine::FogCVars
{
    // 既定は無効。フォグを使わない既存シーンの見た目を一切変えないため、
    // 有効化はエディタ（または CVars.json）からの明示的なオプトインに限る
    CVar<bool> Enabled{
        "r.Fog.Enabled", false,
        "高さフォグを有効にする" };

    CVar<Vector4> Color{
        "r.Fog.Color", Vector4{ 0.62f, 0.68f, 0.75f, 1.0f },
        "フォグ色（リニア HDR。アルファは未使用）。SkyColorBlend が 1 のときは空の色に置き換わる" };


    // ImGui の ColorEdit は [0,1] にクランプされるため、HDR の明るさは色と分けて持つ。
    // シーンの輝度スケール（太陽強度・露出）に合わせるのは基本この値
    CVar<float> ColorIntensity{
        "r.Fog.ColorIntensity", 1.0f,
        "フォグ色の明るさ倍率。Color と掛けた値がリニア HDR のフォグ色になる。"
        "フォグが暗く見えるときはここを上げる",
        CVarRange{ 0.0f, 20.0f } };
    CVar<float> Density{
        "r.Fog.Density", 0.02f,
        "基準高度での消散係数 [1/m]。0.02 なら水平方向 50m で透過率 0.37",
        CVarRange{ 0.0f, 2.0f } };

    CVar<float> HeightFalloff{
        "r.Fog.HeightFalloff", 0.1f,
        "高さ方向の減衰率 [1/m]。0 で高さ非依存（＝距離フォグ）、"
        "大きいほど地表に溜まる。10 以上で境界のくっきりした「霧の海」になる",
        CVarRange{ 0.0f, 20.0f } };

    CVar<float> HeightRefM{
        "r.Fog.HeightRef", 0.0f,
        "Density を与える基準高度 [m]。実質「霧の水面」の高さ",
        CVarRange{ -100.0f, 500.0f } };

    CVar<float> StartDistanceM{
        "r.Fog.StartDistance", 0.0f,
        "フォグが効き始めるカメラからの距離 [m]",
        CVarRange{ 0.0f, 1000.0f } };

    CVar<float> MaxOpacity{
        "r.Fog.MaxOpacity", 1.0f,
        "フォグの最大濃度。1 未満にすると遠景が完全には消えない",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> SkyDistanceM{
        "r.Fog.SkyDistance", 5000.0f,
        "背景（深度 far）ピクセルのレイ長 [m]。0 以下ならカメラのファークリップを使う",
        CVarRange{ 0.0f, 100000.0f } };

    CVar<bool> ApplyToSky{
        "r.Fog.ApplyToSky", true,
        "背景（空・未描画）にもフォグを掛ける。"
        "HeightFalloff = 0 で有効にすると空が一色に塗り潰されるので注意" };

    CVar<float> SkyColorBlend{
        "r.Fog.SkyColorBlend", 1.0f,
        "フォグ色を空の色（Sky-View LUT）へ寄せる量。1 で遠景のフォグが空へ完全に溶ける。"
        "大気を使わないシーンでは自動的に無効になり Color がそのまま残る",
        CVarRange{ 0.0f, 1.0f } };

    CVar<Vector4> SunTint{
        "r.Fog.SunTint", Vector4{ 1.0f, 0.82f, 0.60f, 1.0f },
        "太陽方向でのフォグの色味。基準色（Color または空の色）への倍率として掛かる" };

    CVar<float> SunScatteringGain{
        "r.Fog.SunScatteringGain", 2.5f,
        "太陽方向でのフォグの明るさ倍率。1 で内散乱なし。"
        "太陽ライトが無いフレームは自動的に 1 になる",
        CVarRange{ 1.0f, 8.0f } };

    CVar<float> SunScatteringExponent{
        "r.Fog.SunScatteringExponent", 16.0f,
        "内散乱ローブの鋭さ（cos^n の n）。大きいほど太陽の周りだけが狭く光る",
        CVarRange{ 1.0f, 128.0f } };

    void LoadInto(FogSettings& settings)
    {
        const Vector4& color = Color.Get();
        const Vector4& sunTint = SunTint.Get();

        settings.enabled = Enabled.Get();
        settings.density = Density.Get();
        settings.heightFalloff = HeightFalloff.Get();
        settings.heightRefM = HeightRefM.Get();
        settings.startDistanceM = StartDistanceM.Get();
        settings.maxOpacity = MaxOpacity.Get();
        settings.skyDistanceM = SkyDistanceM.Get();
        settings.applyToSky = ApplyToSky.Get();

        settings.color = Vector3{ color.x, color.y, color.z };
        settings.colorIntensity = ColorIntensity.Get();
        settings.sunTint = Vector3{ sunTint.x, sunTint.y, sunTint.z };
        settings.sunGain = SunScatteringGain.Get();
        settings.sunExponent = SunScatteringExponent.Get();
        settings.skyColorBlend = SkyColorBlend.Get();
    }
}

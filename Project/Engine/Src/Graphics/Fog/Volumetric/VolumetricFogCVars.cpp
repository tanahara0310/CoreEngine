#include "pch.h"
#include "Graphics/Fog/Volumetric/VolumetricFogCVars.h"

namespace CoreEngine::VolumetricFogCVars
{
    CVar<bool> Enabled{
        "r.Fog.Volumetric.Enabled", false,
        "カメラの手前 Distance までの高さフォグを、光と影を持つ 3D の格子で計算する。"
        "r.Fog.Enabled が有効なときだけ効く" };

    CVar<float> Distance{
        "r.Fog.Volumetric.Distance", 150.0f,
        "格子が届く距離 [m]。その先は今の高さフォグの式で続ける",
        CVarRange{ 10.0f, 1000.0f } };

    CVar<int> GridPixelSize{
        "r.Fog.Volumetric.GridPixelSize", 12,
        "格子 1 つが覆う画面の大きさ [px]。小さいほど細かく重い",
        CVarRange{ 8.0f, 32.0f } };

    CVar<int> GridSizeZ{
        "r.Fog.Volumetric.GridSizeZ", 64,
        "奥行きの枚数。多いほど細かく重い",
        CVarRange{ 16.0f, 128.0f } };

    CVar<float> DepthDistribution{
        "r.Fog.Volumetric.DepthDistribution", 32.0f,
        "奥行きの分け方。大きいほど手前が細かく奥が厚い",
        CVarRange{ 1.0f, 128.0f } };

    CVar<Vector4> Albedo{
        "r.Fog.Volumetric.Albedo", Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
        "霧が光を散乱する割合（色）。灰色にすると霧が暗くなる（アルファは未使用）" };

    CVar<float> Anisotropy{
        "r.Fog.Volumetric.Anisotropy", 0.6f,
        "光の散る向きの偏り（位相関数の g）。大きいほど光源の方を向いたときだけ明るい。0 で全方向へ同じ",
        CVarRange{ -0.9f, 0.95f } };

    CVar<float> AmbientScale{
        "r.Fog.Volumetric.AmbientScale", 1.0f,
        "空の光への倍率（大気のあるシーンは面の空の環境光と同じ明るさに、さらにこの値を掛ける）",
        CVarRange{ 0.0f, 4.0f } };

    CVar<float> HistoryWeight{
        "r.Fog.Volumetric.HistoryWeight", 0.9f,
        "前のフレームの光を混ぜる量。大きいほど縞とざらつきが消えるが、動く影の追従が遅れる。0 で混ぜない",
        CVarRange{ 0.0f, 0.98f } };

    CVar<int> DebugView{
        "r.Fog.Volumetric.DebugView", 0,
        "デバッグ表示。0 = なし、1 = 透過率、2 = 溜まった光、3 = 奥行きの何枚目か、4 = 格子の奥の端での雲の影",
        CVarRange{ 0.0f, 4.0f }, CVarFlags::NoSave };
}

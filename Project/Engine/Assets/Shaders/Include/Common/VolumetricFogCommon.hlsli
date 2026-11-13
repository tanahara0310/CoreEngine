#pragma once

/// @file VolumetricFogCommon.hlsli
/// @brief ボリュメトリックフォグの格子の写像と光の式
/// @details 格子は画面に揃えた 3D の配列。奥行きは d(u) = distance * ((1 + c)^u - 1) / c（u = スライス / 枚数）。
///          格子を作る CS と全画面の合成が同じ関数を呼ぶ。

#include "ShaderMath.hlsli"

/// @brief 格子を照らす平行光源 1 灯（C++ 側 VolumetricFog::DirectionalLight と 1 対 1）
struct VolumetricFogDirectionalLight
{
    float3 direction;  ///< 光の進行方向（正規化済み）
    float  pad0;
    float3 irradiance; ///< 照度（色 × 強度。シェーダー単位）
    float  pad1;
};

/// @brief 格子を照らす平行光源の最大数
static const uint kVolumetricFogMaxDirectionalLights = 4;

/// @brief ボリュメトリックフォグの定数（C++ 側 VolumetricFog::Constants と 1 対 1）
struct VolumetricFogParameters
{
    float4x4 invViewProj;        ///< View*Projection の逆行列（行ベクトル規約）
    float3   cameraWorldPos;     ///< カメラのワールド座標 [m]
    float    distance;           ///< 格子が届く距離 [m]
    uint3    gridSize;           ///< 格子の数（横・縦・奥行き）
    float    depthDistribution;  ///< 奥ほど厚くする度合い（c）
    float2   screenToGrid;       ///< 画面の UV → 格子の UV の倍率
    float2   gridPixelSize;      ///< 格子 1 つの画面の大きさを UV で表した値
    float3   albedo;             ///< 散乱の割合
    float    phaseG;             ///< 位相関数の g
    float    density;            ///< 基準高度での消散係数 [1/m]
    float    heightFalloff;      ///< 高さ方向の減衰率 [1/m]
    float    heightRef;          ///< 基準高度 [m]
    float    startDistance;      ///< 霧が効き始める距離 [m]
    float3   fallbackAmbient;    ///< 空の SH を使わないときの空の光（輝度）
    uint     skyAmbientEnabled;  ///< 1 なら空の光を SH から求める
    float    skyAmbientScale;    ///< 空の SH の値への倍率
    uint     directionalCount;   ///< 霧を照らす平行光源の数
    uint     debugView;          ///< デバッグ表示（0 = なし）
    float    pad0;
    VolumetricFogDirectionalLight directional[kVolumetricFogMaxDirectionalLights];
};

/// @brief 奥行きの連続のスライス座標 [0, N] から距離 [m] を求める
float VolumetricFogSliceToDistance(VolumetricFogParameters p, float slice)
{
    const float u = saturate(slice / float(p.gridSize.z));
    const float c = max(p.depthDistribution, 1.0e-3f);
    return p.distance * (pow(1.0f + c, u) - 1.0f) / c;
}

/// @brief 距離 [m] から奥行きの連続のスライス座標 [0, N] を求める
float VolumetricFogDistanceToSlice(VolumetricFogParameters p, float distanceM)
{
    const float c = max(p.depthDistribution, 1.0e-3f);
    const float d = clamp(distanceM, 0.0f, p.distance);
    return float(p.gridSize.z) * log(1.0f + c * d / max(p.distance, 1.0e-3f)) / log(1.0f + c);
}

/// @brief Henyey-Greenstein の位相関数
/// @param cosTheta 視線の向きと光源へ向かう向きの内積
float VolumetricFogPhaseHG(float g, float cosTheta)
{
    const float g2 = g * g;
    const float denom = max(1.0f + g2 - 2.0f * g * cosTheta, 1.0e-4f);
    return (1.0f - g2) / (FOUR_PI * denom * sqrt(denom));
}

/// @brief 全方向の空の平均の輝度を求める
/// @param skyIrradianceSH 放射照度/π へ畳み込み済みの SH9 係数（0 次は畳み込みで変わらない）
float3 VolumetricFogSkyAmbient(VolumetricFogParameters p, StructuredBuffer<float4> skyIrradianceSH)
{
    if (p.skyAmbientEnabled != 0)
    {
        return max(skyIrradianceSH[0].rgb * 0.282095f, 0.0f) * p.skyAmbientScale;
    }
    return p.fallbackAmbient;
}

/// @brief 影の無い点で視線の向きへ散乱する光の源（散乱係数を掛ける前）
/// @param viewDir カメラから点へ向かう向き（正規化済み）
/// @param ambient 空の平均の輝度
float3 VolumetricFogUnshadowedSource(VolumetricFogParameters p, float3 viewDir, float3 ambient)
{
    float3 source = ambient;
    [loop]
    for (uint i = 0; i < p.directionalCount; ++i)
    {
        const VolumetricFogDirectionalLight light = p.directional[i];
        const float cosTheta = dot(viewDir, -light.direction);
        source += light.irradiance * VolumetricFogPhaseHG(p.phaseG, cosTheta);
    }
    return source;
}

/// @brief 1 枚のスライスの中で散乱して視線へ届く光（スライスの手前の面で見た値）
/// @param scattered 散乱係数 × 光の源
/// @param extinction 消散係数 [1/m]
/// @param sliceLength スライスの厚さ [m]
float3 VolumetricFogIntegrateSlice(float3 scattered, float extinction, float sliceLength)
{
    const float x = extinction * sliceLength;
    const float f = (x > 1.0e-4f) ? (1.0f - exp(-x)) / x : 1.0f - 0.5f * x;
    return scattered * (sliceLength * f);
}

/// @brief 積み上げた格子から、カメラから distanceM までの光と透過率を引く
/// @param integrated 各スライスの奥の面までの光（rgb）と透過率（a）
/// @param gridUV 格子の UV（画面の UV × screenToGrid）
/// @return rgb = 溜まった光、a = 透過率
float4 VolumetricFogSampleIntegrated(
    VolumetricFogParameters p, Texture3D<float4> integrated, SamplerState linearClamp,
    float2 gridUV, float distanceM)
{
    const float slice = VolumetricFogDistanceToSlice(p, distanceM);
    const float w = (max(slice, 1.0f) - 0.5f) / float(p.gridSize.z);
    const float4 value = integrated.SampleLevel(linearClamp, float3(gridUV, w), 0);
    return lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), value, saturate(slice));
}

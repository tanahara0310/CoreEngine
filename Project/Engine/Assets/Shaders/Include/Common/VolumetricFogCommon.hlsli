#pragma once

/// @file VolumetricFogCommon.hlsli
/// @brief ボリュメトリックフォグの格子の写像と光の式
/// @details 格子は画面に揃えた 3D の配列。奥行きは d(u) = distance * ((1 + c)^u - 1) / c（u = スライス / 枚数）。
///          格子を作る CS と全画面の合成が同じ関数を呼ぶ。

#include "ShaderMath.hlsli"
#include "../../Cloud/Common/CloudShadowCommon.hlsli"

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

/// @brief 太陽から見た深さの画像を使う光源が無いときの番号
static const uint kVolumetricFogNoSunDepthLight = 0xFFFFFFFFu;

/// @brief 格子を照らす点光源・スポット・エリア 1 灯（C++ 側 VolumetricFog::LocalLight と 1 対 1）
struct VolumetricFogLocalLight
{
    float3 position;   ///< 光源の位置（エリアは発光面の中心）[m]
    float  range;      ///< 届く距離 [m]
    float3 intensity;  ///< 色 × 強度 × 霧を照らす強さ（シェーダー単位。1/d² を掛けると照度）
    uint   type;       ///< kVolumetricFogLocalLight*
    float3 direction;  ///< スポットの向き・エリアの発光面の法線（正規化済み）
    float  cosOuter;   ///< スポットの外角の cos
    float3 right;      ///< エリアの発光面の右の向き
    float  cosInner;   ///< スポットの内角の cos
    float3 up;         ///< エリアの発光面の上の向き
    float  halfWidth;  ///< エリアの発光面の幅の半分 [m]
    float  halfHeight; ///< エリアの発光面の高さの半分 [m]
    float3 pad;
};

/// @brief 格子を照らす点光源・スポット・エリアの最大数
static const uint kVolumetricFogMaxLocalLights = 40;

/// @brief 局所のライトの種類（C++ 側 VolumetricFog::LocalLightType と一致）
static const uint kVolumetricFogLocalLightPoint = 0;
static const uint kVolumetricFogLocalLightSpot = 1;
static const uint kVolumetricFogLocalLightArea = 2;

/// @brief 格子を照らす点光源・スポット・エリアの定数（C++ 側 VolumetricFog::LocalLightConstants と 1 対 1）
struct VolumetricFogLocalLights
{
    uint  count;
    uint3 pad;
    VolumetricFogLocalLight lights[kVolumetricFogMaxLocalLights];
};

/// @brief ボリュメトリックフォグの定数（C++ 側 VolumetricFog::Constants と 1 対 1）
struct VolumetricFogParameters
{
    float4x4 invViewProj;        ///< ジッタ無しの View*Projection の逆行列（行ベクトル規約）
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
    float4x4 prevViewProj;       ///< 前フレームのジッタ無しの View*Projection
    float3   prevCameraWorldPos; ///< 前フレームのカメラのワールド座標 [m]
    float    historyWeight;      ///< 前フレームの光を混ぜる量
    float3   frameJitter;        ///< 格子 1 つの中の今フレームの点（xy は -0.5〜0.5、z は 0〜1）
    uint     historyValid;       ///< 1 なら前フレームの光を混ぜてよい
    float2   cloudShadowRegionCenterXZ; ///< 雲の影のマップの中心（ワールド XZ）[m]
    float    cloudShadowRegionSize;     ///< 雲の影のマップの一辺 [m]
    float    cloudShadowAnchorY;        ///< 雲の影のマップを置く高さ（雲の底）[m]
    float    cloudShadowEdgeFadeStart;  ///< マップの端で影を消し始める正規化半径
    float    cloudShadowStrength;       ///< 雲の影の強さ（面と同じ値）
    uint     cloudShadowEnabled;        ///< 1 なら雲の影を引く
    float    pad1;
    float3   sunDepthOrigin;     ///< 太陽から見た深さの画像の面の中心（レイの始点の面）[m]
    float    sunDepthInvSize;    ///< 画像の一辺 [m] の逆数
    float3   sunDepthRight;      ///< 画像の右の向き（ワールド）
    float    sunDepthBias;       ///< 日なたとみなす距離の余裕 [m]
    float3   sunDepthUp;         ///< 画像の上の向き（ワールド）
    float    sunDepthRange;      ///< レイの長さ [m]（当たらなかった所はこれより大きい値）
    float3   sunDepthDir;        ///< レイの向き（光の進行方向）
    uint     sunDepthLightIndex; ///< 画像を使う光源の directional の番号（kVolumetricFogNoSunDepthLight = 無し）
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

/// @brief 点から光源の向きの雲の透過率（面の雲の影と同じ強さで弱めた値）
/// @param toLight 点から光源へ向かう向き（正規化済み）
float VolumetricFogCloudShadow(
    VolumetricFogParameters p, Texture2D<float> cloudShadowMap, SamplerState linearClamp,
    float3 worldPos, float3 toLight)
{
    if (p.cloudShadowEnabled == 0)
    {
        return 1.0f;
    }
    CloudShadowConstants cloudShadow;
    cloudShadow.regionCenterX = p.cloudShadowRegionCenterXZ.x;
    cloudShadow.regionCenterZ = p.cloudShadowRegionCenterXZ.y;
    cloudShadow.regionSizeM = p.cloudShadowRegionSize;
    cloudShadow.anchorWorldY = p.cloudShadowAnchorY;
    cloudShadow.edgeFadeStart = p.cloudShadowEdgeFadeStart;
    cloudShadow.sceneStrength = p.cloudShadowStrength;
    cloudShadow.rowOffset = 0u;
    cloudShadow.pad1 = 0.0f;
    const float shadow = SampleCloudShadow(cloudShadowMap, linearClamp, worldPos, toLight, cloudShadow);
    return lerp(1.0f, shadow, p.cloudShadowStrength);
}

/// @brief 太陽から見た深さの画像の UV と、点の面からの距離を求める
/// @param distanceFromPlane 画像の面から光の進む向きへ測った点の距離 [m]
float2 VolumetricFogSunDepthUV(VolumetricFogParameters p, float3 worldPos, out float distanceFromPlane)
{
    const float3 local = worldPos - p.sunDepthOrigin;
    distanceFromPlane = dot(local, p.sunDepthDir);
    return float2(dot(local, p.sunDepthRight), -dot(local, p.sunDepthUp)) * p.sunDepthInvSize + 0.5f;
}

/// @brief 点が物に遮られずに光源から見えている割合（太陽から見た深さの画像を 2x2 で比べて双線形で混ぜる）
/// @return 画像を使う光源が無い・画像の範囲の外なら 1
float VolumetricFogSunDepthShadow(
    VolumetricFogParameters p, Texture2D<float> sunDepth, SamplerState linearClamp, float3 worldPos)
{
    if (p.sunDepthLightIndex == kVolumetricFogNoSunDepthLight)
    {
        return 1.0f;
    }
    float distanceFromPlane;
    const float2 uv = VolumetricFogSunDepthUV(p, worldPos, distanceFromPlane);
    if (any(uv <= 0.0f) || any(uv >= 1.0f))
    {
        return 1.0f;
    }

    uint width;
    uint height;
    sunDepth.GetDimensions(width, height);
    const float2 f = frac(uv * float2(width, height) - 0.5f);
    // x = 左下・y = 右下・z = 右上・w = 左上
    const float4 depth = sunDepth.GatherRed(linearClamp, uv);
    const float4 lit = float4(distanceFromPlane - p.sunDepthBias < depth);
    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}

/// @brief 点で視線の向きへ散乱する光の源（散乱係数を掛ける前）
/// @param viewDir カメラから点へ向かう向き（正規化済み）
/// @param ambient 空の平均の輝度
/// @param worldPos 雲の影を引く点のワールド座標 [m]
/// @param sunDepthShadow 太陽から見た深さの画像を使う光源への物の影（VolumetricFogSunDepthShadow）
float3 VolumetricFogSource(
    VolumetricFogParameters p, float3 viewDir, float3 ambient, float3 worldPos,
    Texture2D<float> cloudShadowMap, SamplerState linearClamp, float sunDepthShadow)
{
    float3 source = ambient;
    [loop]
    for (uint i = 0; i < p.directionalCount; ++i)
    {
        const VolumetricFogDirectionalLight light = p.directional[i];
        const float3 toLight = -light.direction;
        const float cosTheta = dot(viewDir, toLight);
        float shadow = VolumetricFogCloudShadow(p, cloudShadowMap, linearClamp, worldPos, toLight);
        if (i == p.sunDepthLightIndex)
        {
            shadow *= sunDepthShadow;
        }
        source += light.irradiance * (VolumetricFogPhaseHG(p.phaseG, cosTheta) * shadow);
    }
    return source;
}

/// @brief 点光源・スポット・エリア 1 灯から点へ届く照度（面の DeferredLighting と同じ逆二乗・範囲・コーン・発光面の式）
/// @param minDistance 逆二乗に使う距離の下限 [m]（光源のすぐ近くで格子より細かく尖らせない）
/// @param toLight 点から光源（エリアは発光面の上で点に最も近い点）へ向かう向き
float3 VolumetricFogLocalLightIrradiance(
    VolumetricFogLocalLight light, float3 worldPos, float minDistance, out float3 toLight)
{
    toLight = float3(0.0f, 1.0f, 0.0f);

    float3 target = light.position;
    float shape = 1.0f;
    if (light.type == kVolumetricFogLocalLightArea)
    {
        // 発光面の上で点に最も近い点と、発光面を面の向きへ伸ばした筒からのはみ出し
        const float3 offset = worldPos + light.direction * dot(light.position - worldPos, light.direction)
            - light.position;
        const float2 local = float2(dot(offset, light.right), dot(offset, light.up));
        const float2 halfSize = float2(light.halfWidth, light.halfHeight);
        const float2 clamped = clamp(local, -halfSize, halfSize);
        target = light.position + light.right * clamped.x + light.up * clamped.y;
        const float outside = length(max(abs(local) - halfSize, 0.0f));
        if (outside > 0.001f)
        {
            shape = 1.0f - saturate(outside / max(max(halfSize.x, halfSize.y), 1.0e-4f));
            shape = shape * shape * shape;
        }
    }

    const float3 lightVector = target - worldPos;
    const float distanceSq = dot(lightVector, lightVector);
    if (distanceSq >= light.range * light.range)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float distanceM = sqrt(distanceSq);
    toLight = lightVector / max(distanceM, 1.0e-3f);

    const float ratio = distanceM / light.range;
    const float ratio2 = ratio * ratio;
    const float window = saturate(1.0f - ratio2 * ratio2);
    float attenuation = window * window / max(distanceSq, max(minDistance * minDistance, 0.01f));

    if (light.type == kVolumetricFogLocalLightSpot)
    {
        const float cosTheta = dot(-toLight, light.direction);
        attenuation *= saturate((cosTheta - light.cosOuter) / max(light.cosInner - light.cosOuter, 1.0e-4f));
    }
    else if (light.type == kVolumetricFogLocalLightArea)
    {
        attenuation *= shape * max(dot(light.direction, -toLight), 0.0f);
    }
    return light.intensity * attenuation;
}

/// @brief 画面の UV と距離から、格子の座標（テクスチャの UVW）を求める
/// @param distanceM カメラからの距離 [m]
float3 VolumetricFogGridUVW(VolumetricFogParameters p, float2 screenUV, float distanceM)
{
    return float3(screenUV * p.screenToGrid,
        VolumetricFogDistanceToSlice(p, distanceM) / float(p.gridSize.z));
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

// ============================================================
// 水が引いたあとの濡れ跡（DeferredLighting.PS.hlsl と RT の当たった点の照明が使う関数群）
// ------------------------------------------------------------
// 濡れ具合は高さだけで決まる表（C++ 側 WaterWetnessHistory が作る）を、面の高さで引く。
//   - 表の下端は今の水面。下端から上へ等間隔に濡れ具合が並ぶ
//   - 水面より下の面は濡れている（深い所は水中の扱いに任せて弱める）
//   - 水面の範囲の外は濡らさない
//
// 【include する側の決まり】表の n 番目の濡れ具合を返す LoadWetSurfaceSample(uint n) を
// 定義すること（表の置き場所はシェーダーごとに違う）。
// ============================================================
#ifndef WET_SURFACE_INCLUDED
#define WET_SURFACE_INCLUDED

/// @brief 表の n 番目の濡れ具合（include する側が定義する）
float LoadWetSurfaceSample(uint n);

/// @brief 表の標本数（C++ 側 kWaterWetnessSampleCount と同じ）
static const uint kWetSurfaceSampleCount = 64u;
/// @brief 表を詰める float4 の数
static const uint kWetSurfaceTableVectorCount = kWetSurfaceSampleCount / 4u;
/// @brief 水面より下で濡れ跡を弱め切る深さ [m]
static const float kWetSurfaceBelowFadeDepth = 0.5f;
/// @brief 濡れきった面の粗さの上限（水の膜で表面がなめらかになる）
static const float kWetSurfaceRoughness = 0.25f;

/// @brief 面の濡れ具合 [0,1]
/// @param bottomHeight    表の下端（今の水面の高さ）[m]
/// @param samplesPerMeter 1 m あたりの標本の数
float SampleWetSurface(
    float3 worldPos,
    float bottomHeight,
    float samplesPerMeter,
    float2 regionCenterXZ,
    float2 regionHalfExtentXZ)
{
    if (any(abs(worldPos.xz - regionCenterXZ) > regionHalfExtentXZ))
    {
        return 0.0f;
    }
    const float heightAboveWater = worldPos.y - bottomHeight;
    if (heightAboveWater < 0.0f)
    {
        return saturate(1.0f + heightAboveWater / kWetSurfaceBelowFadeDepth);
    }
    const float position = heightAboveWater * samplesPerMeter;
    const float lastSample = (float)(kWetSurfaceSampleCount - 1u);
    if (position >= lastSample)
    {
        return 0.0f;
    }
    const uint index = (uint)position;
    return lerp(LoadWetSurfaceSample(index), LoadWetSurfaceSample(index + 1u), position - (float)index);
}

/// @brief 濡れ具合で粗さを下げる
float ApplyWetRoughness(float roughness, float wetness)
{
    return lerp(roughness, min(roughness, kWetSurfaceRoughness), wetness);
}

#endif // WET_SURFACE_INCLUDED

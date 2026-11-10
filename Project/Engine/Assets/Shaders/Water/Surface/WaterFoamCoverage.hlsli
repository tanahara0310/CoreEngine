// ============================================================
// 泡の被覆率（whitecap / shore foam）（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 泡が「どこにどれだけあるか」を、水面のうち泡が占める割合（被覆率 [0,1]）として求める。
// 見た目（模様・色）は WaterFoamAppearance.hlsli が担当する。
// ============================================================
#ifndef WATER_FOAM_COVERAGE_INCLUDED
#define WATER_FOAM_COVERAGE_INCLUDED

#include "WaterSurfaceResources.hlsli"
#include "../Common/WaterWhitecapCoverage.hlsli"
#include "../Common/WaterAppearanceConstants.hlsli"

// ---- 岸際泡（shore foam）----
// 範囲の外で泡帯が消える水深 [m]（水面の点の真下の鉛直水深）
static const float kShoreFoamDepthMeters = 0.6f;
// 汀線エッジ（この水深以浅）は被覆率満量＝ほぼ連続した白いシートになる
static const float kShoreFoamEdgeMeters = 0.15f;
// 海底の高さの範囲の端で、視線の延長で測った水深へ移す幅（範囲の一辺に対する割合）
static const float kSeabedWindowEdgeFade = 0.1f;

/// @brief 白波の被覆率の内訳
struct WhitecapFoamCoverage
{
    float instant; ///< いま砕けている泡 [0,1]
    float residue; ///< 砕けた後に残っている泡 [0,1]
};

/// @brief 白波の被覆率を、いま砕けている泡と残っている泡に分けて求める（FFTOcean 専用）
/// @details いま砕けている泡は合成ヤコビアン detJ < gFoamBias（波頭の圧縮）。
///          残っている泡は FFTOceanFoamAccumulate.CS が同じ判定で溜めて減らした格子を読む。
///          Gerstner 経路はヤコビアンを持たないため常に 0
///          （gUseFFTOceanNormalMap は FFT 使用フラグと同値で更新される）。
WhitecapFoamCoverage ComputeWhitecapFoamCoverage(float2 worldXZ)
{
    WhitecapFoamCoverage coverage;
    coverage.instant = 0.0f;
    coverage.residue = 0.0f;
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return coverage;
    }
    const float detJ = ComputeFFTCombinedDetJ(
        worldXZ, gFFTOceanJacobian, gSampler, gFoamCascadeWeights, gWaveGroupPhase);
    coverage.instant = ComputeWhitecapInstant(detJ, gFoamBias, gFoamGain);

    WhitecapResidueWindows windows;
    windows.originsXZ = gFoamResidueOriginsXZ;
    windows.invSizes = gFoamResidueInvSizes;
    coverage.residue = SampleWhitecapResidue(
        worldXZ, gFFTOceanFoam, gLinearClamp, gFoamDriftOffsetXZ, windows, gFoamResidueMeanCoverage);
    return coverage;
}

/// @brief 海底の高さの範囲の中での位置と、範囲の端からの近さ
/// @param uv 範囲の中での位置 [0,1]
/// @return 範囲の端の kSeabedWindowEdgeFade の幅で 0 から 1 へ上がる値（範囲の外は 0）
float ComputeSeabedWindowInside(float2 worldXZ, out float2 uv)
{
    uv = (worldXZ - gSeabedOriginXZ) * gSeabedInvSize;
    const float2 edgeDistance = min(uv, 1.0f - uv);
    return saturate(min(edgeDistance.x, edgeDistance.y) / kSeabedWindowEdgeFade);
}

/// @brief 汀線エッジ（水深 kShoreFoamEdgeMeters より浅い所）の被覆率 [0,1]
float ComputeShoreFoamEdge(float verticalDepth)
{
    return 1.0f - smoothstep(0.0f, kShoreFoamEdgeMeters, verticalDepth);
}

/// @brief 岸の泡に使う、水面の点の真下の鉛直水深 [m] を返す
/// @param surfacePosition 水面の点（頂点変位後のワールド座標）
/// @param fallbackDepth   海底の高さの範囲の外で使う水深（視線の延長で測った鉛直水深）
/// @details 範囲の中は RTWaterSeabedPass が真上から測った海底の高さとの差。
///          範囲の端の kSeabedWindowEdgeFade の幅で fallbackDepth へなだらかに移す。
///          水面の点が物や陸に占められている所は負になる
float ResolveShoreFoamDepth(float3 surfacePosition, float fallbackDepth)
{
    if (gSeabedEnabled == 0)
    {
        return fallbackDepth;
    }
    float2 uv;
    const float inside = ComputeSeabedWindowInside(surfacePosition.xz, uv);
    if (inside <= 0.0f)
    {
        return fallbackDepth;
    }
    const float seabedY = gWaterSeabedHeight.SampleLevel(gLinearClamp, uv, 0.0f);
    return lerp(fallbackDepth, surfacePosition.y - seabedY, inside);
}

/// @brief 水深だけで決める岸際泡（shore foam）の被覆率 [0,1]（岸の泡を進める範囲の外で使う）
/// @param verticalDepth 水面の点の真下の鉛直水深 [m]（ResolveShoreFoamDepth）
/// @details 2 段構造: 汀線エッジ（〜0.15m）は被覆率満量＝連続した白いシート、
///          外側（〜0.6m）は被覆率 0.9→0 のフェード＝dissolve でレース状に割れる。
float ComputeShoreFoamMask(float verticalDepth)
{
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return 0.0f;
    }

    // 外側の泡帯: 水深 0 → kShoreFoamDepthMeters の連続フェード
    const float band = 1.0f - smoothstep(0.0f, kShoreFoamDepthMeters, verticalDepth);
    // 汀線エッジ: ごく浅い所は満量（レースの穴が埋まり白いシートになる）
    const float edge = ComputeShoreFoamEdge(verticalDepth);

    return max(band * kWaterShoreFoamStrength, edge);
}

/// @brief 岸の泡の被覆率と、模様を評価する位置へのずれ
struct ShoreFoamResult
{
    float coverage;      ///< 被覆率 [0,1]
    float2 patternShift; ///< 模様を評価する位置へ足すずれ [m]（今の位置 → 水の粒の静止位置）
};

/// @brief 岸の泡の状態（被覆率, 寄せ・引きのずれ x, z）を読む
float3 SampleShoreFoamState(float2 worldXZ)
{
    return gWaterShoreFoam.SampleLevel(gLinearClamp, (worldXZ - gSeabedOriginXZ) * gSeabedInvSize, 0.0f).xyz;
}

/// @brief 岸の泡の被覆率を求める
/// @param surfacePosition 水面の点（頂点変位後のワールド座標）
/// @param fallbackDepth   範囲の外で使う水深（視線の延長で測った鉛直水深）
/// @details 範囲の中は WaterShoreFoamPass が進めた泡を、この点に今いる水の粒の静止位置
///          （静止位置＋寄せ・引きのずれ＝今の位置 を反復で解く）で読む。水深 kShoreFoamEdgeMeters より
///          浅い水際はその瞬間の水深で満量にする。範囲の外と範囲の端は ComputeShoreFoamMask へ移す
ShoreFoamResult ResolveShoreFoam(float3 surfacePosition, float fallbackDepth)
{
    ShoreFoamResult result;
    result.coverage = 0.0f;
    result.patternShift = float2(0.0f, 0.0f);
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return result;
    }

    const float verticalDepth = ResolveShoreFoamDepth(surfacePosition, fallbackDepth);
    const float depthOnly = ComputeShoreFoamMask(verticalDepth);
    if (gShoreFoamEnabled == 0 || gSeabedEnabled == 0)
    {
        result.coverage = depthOnly;
        return result;
    }

    float2 uv;
    const float inside = ComputeSeabedWindowInside(surfacePosition.xz, uv);
    if (inside <= 0.0f)
    {
        result.coverage = depthOnly;
        return result;
    }

    float2 restXZ = surfacePosition.xz;
    [unroll]
    for (int i = 0; i < 2; ++i)
    {
        restXZ = surfacePosition.xz - SampleShoreFoamState(restXZ).yz;
    }
    const float simulated = SampleShoreFoamState(restXZ).x;
    const float edge = ComputeShoreFoamEdge(verticalDepth);

    result.coverage = lerp(depthOnly, max(simulated, edge), inside);
    result.patternShift = (restXZ - surfacePosition.xz) * inside;
    return result;
}

#endif // WATER_FOAM_COVERAGE_INCLUDED

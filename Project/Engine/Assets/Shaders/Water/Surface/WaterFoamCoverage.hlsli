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
// 砕けた波の泡を描く所で、WaterShoreFoamPass が進めた岸の泡に掛ける倍率
static const float kShoreFoamWithBreakFoamScale = 0.35f;
// 砕けた波の泡を描く所で、水の中の気泡の濁りを満量にする鉛直水深 [m]（これより薄い膜ほど濁りを減らす）
static const float kSwashFilmBubbleDepthMeters = 0.2f;

// ---- 物がかき回した所の泡 ----
// 格子の一辺のマス数（WaterObjectFoam::kResolution と同じ値）
static const float kObjectFoamResolution = 1024.0f;
// 格子の端から内側へ薄める幅 [マス]
static const float kObjectFoamEdgeCells = 16.0f;

/// @brief 白波の被覆率の内訳
struct WhitecapFoamCoverage
{
    float instant;        ///< いま砕けている泡 [0,1]
    float residue;        ///< 砕けた後に残っている水面の泡 [0,1]
    float residueBubbles; ///< 砕けた後に残っている水の中の気泡 [0,1]
};

/// @brief 白波の被覆率を、いま砕けている泡と残っている泡に分けて求める（FFTOcean 専用）
/// @details いま砕けている泡は合成ヤコビアン detJ < gFoamBias（波頭の圧縮）。
///          残っている泡と気泡は FFTOceanFoamAccumulate.CS が同じ判定で溜めて減らした格子を読む。
///          Gerstner 経路はヤコビアンを持たないため常に 0
///          （gUseFFTOceanNormalMap は FFT 使用フラグと同値で更新される）。
WhitecapFoamCoverage ComputeWhitecapFoamCoverage(float2 worldXZ)
{
    WhitecapFoamCoverage coverage;
    coverage.instant = 0.0f;
    coverage.residue = 0.0f;
    coverage.residueBubbles = 0.0f;
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
    const float2 residue = SampleWhitecapResidue(
        worldXZ, gFFTOceanFoam, gLinearClamp, gFoamDriftOffsetXZ, windows, gFoamResidueMeanCoverage);
    coverage.residue = residue.x;
    coverage.residueBubbles = residue.y;
    return coverage;
}

/// @brief 物がかき回した所の泡
struct ObjectFoamSample
{
    float foam;             ///< 水面の泡の被覆率 [0,1]
    float bubbles;          ///< 水の中の気泡の量 [0,1]
    float2 patternVelocity; ///< 模様を動かす速さ [m/s]
};

/// @brief 物の泡の格子の中の位置と、格子の端からの近さ
/// @param uv 格子の中の位置 [0,1]
/// @return 格子の端の kObjectFoamEdgeCells マスで 0 から 1 へ上がる値（格子の外と止まっている間は 0）
float ComputeObjectFoamInside(float2 baseWorldXZ, out float2 uv)
{
    uv = (baseWorldXZ - gObjectFoamOriginXZ) * gObjectFoamInvSize;
    if (gObjectFoamEnabled == 0)
    {
        return 0.0f;
    }
    const float2 edgeCells = min(uv, 1.0f - uv) * kObjectFoamResolution;
    return saturate(min(edgeCells.x, edgeCells.y) / kObjectFoamEdgeCells);
}

/// @brief 物がかき回した所の泡と気泡を読む（WaterObjectFoamAdvance.CS が進めたもの）
/// @param baseWorldXZ 変位前の参照格子座標（FFT でない水は世界の XZ）
ObjectFoamSample SampleObjectFoam(float2 baseWorldXZ)
{
    ObjectFoamSample sample;
    sample.foam = 0.0f;
    sample.bubbles = 0.0f;
    sample.patternVelocity = float2(0.0f, 0.0f);
    float2 uv;
    const float inside = ComputeObjectFoamInside(baseWorldXZ, uv);
    [branch]
    if (inside > 0.0f)
    {
        const float4 state = gWaterObjectFoam.SampleLevel(gLinearClamp, uv, 0.0f);
        sample.foam = saturate(state.x) * inside;
        sample.bubbles = saturate(state.y) * inside;
        sample.patternVelocity = state.zw;
    }
    return sample;
}

/// @brief 物が起こした水の流れ [m/s]（格子の外と止まっている間は 0）
float2 SampleObjectFoamFlow(float2 baseWorldXZ)
{
    float2 uv;
    const float inside = ComputeObjectFoamInside(baseWorldXZ, uv);
    [branch]
    if (inside > 0.0f)
    {
        return gWaterObjectFoamFlow.SampleLevel(gLinearClamp, uv, 0.0f) * inside;
    }
    return float2(0.0f, 0.0f);
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
/// @param edgeScale     汀線エッジの被覆率に掛ける倍率
/// @details 2 段構造: 汀線エッジ（〜0.15m）は被覆率満量＝連続した白いシート、
///          外側（〜0.6m）は被覆率 0.9→0 のフェード＝dissolve でレース状に割れる。
float ComputeShoreFoamMask(float verticalDepth, float edgeScale)
{
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return 0.0f;
    }

    // 外側の泡帯: 水深 0 → kShoreFoamDepthMeters の連続フェード
    const float band = 1.0f - smoothstep(0.0f, kShoreFoamDepthMeters, verticalDepth);
    // 汀線エッジ: ごく浅い所は満量（レースの穴が埋まり白いシートになる）
    const float edge = ComputeShoreFoamEdge(verticalDepth) * edgeScale;

    return max(band * kWaterShoreFoamStrength, edge);
}

/// @brief 岸の泡の被覆率と、模様を評価する位置へのずれ
struct ShoreFoamResult
{
    float coverage;      ///< 被覆率 [0,1]
    float2 patternShift; ///< 模様を評価する位置へ足すずれ [m]（今の位置 → 水の粒の静止位置）
    float depth;         ///< 水面の点の真下の鉛直水深 [m]（ResolveShoreFoamDepth。泡を描かないときは 0）
};

/// @brief 岸の泡の状態（被覆率, 寄せ・引きのずれ x, z）を読む
float3 SampleShoreFoamState(float2 worldXZ)
{
    return gWaterShoreFoam.SampleLevel(gLinearClamp, (worldXZ - gSeabedOriginXZ) * gSeabedInvSize, 0.0f).xyz;
}

/// @brief 岸の泡の被覆率を求める
/// @param surfacePosition 水面の点（頂点変位後のワールド座標）
/// @param fallbackDepth   範囲の外で使う水深（視線の延長で測った鉛直水深）
/// @param simulatedScale  WaterShoreFoamPass が進めた泡に掛ける倍率
/// @param edgeScale       水深 kShoreFoamEdgeMeters より浅い水際の満量の被覆率に掛ける倍率
/// @details 範囲の中は WaterShoreFoamPass が進めた泡を、この点に今いる水の粒の静止位置
///          （静止位置＋寄せ・引きのずれ＝今の位置 を反復で解く）で読む。水深 kShoreFoamEdgeMeters より
///          浅い水際はその瞬間の水深で満量（× edgeScale）にする。範囲の外と範囲の端は ComputeShoreFoamMask へ移す
ShoreFoamResult ResolveShoreFoam(float3 surfacePosition, float fallbackDepth, float simulatedScale, float edgeScale)
{
    ShoreFoamResult result;
    result.coverage = 0.0f;
    result.patternShift = float2(0.0f, 0.0f);
    result.depth = 0.0f;
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return result;
    }

    const float verticalDepth = ResolveShoreFoamDepth(surfacePosition, fallbackDepth);
    result.depth = verticalDepth;
    const float depthOnly = ComputeShoreFoamMask(verticalDepth, edgeScale);
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
    const float simulated = SampleShoreFoamState(restXZ).x * simulatedScale;
    const float edge = ComputeShoreFoamEdge(verticalDepth) * edgeScale;

    result.coverage = lerp(depthOnly, max(simulated, edge), inside);
    result.patternShift = (restXZ - surfacePosition.xz) * inside;
    return result;
}

#endif // WATER_FOAM_COVERAGE_INCLUDED

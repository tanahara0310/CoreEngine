// ============================================================
// 泡の被覆率（whitecap / shore foam）（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 泡が「どこにどれだけあるか」を、水面のうち泡が占める割合（被覆率 [0,1]）として求める。
// 見た目（模様・色）は WaterFoamAppearance.hlsli が担当する。
//
// 【飽和対策 3 点セットの一角】瞬時マスクはカスケード重み付き合成 detJ
// （ComputeFFTCombinedDetJ）を必ず通すこと。波群エンベロープは蓄積項のみに掛ける
// （瞬時項は合成 detJ 内で適用済み＝二重適用禁止）。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterFrameConstants(b5) の後で
// include すること。以下に暗黙依存:
//   資源    : gFFTOceanJacobian / gFFTOceanFoam / gSampler / gWaterSeabedHeight /
//             gWaterShoreFoam / gLinearClamp
//   cbuffer : gFoamEnabled / gFoamBias / gFoamGain / gFoamCascadeWeights /
//             gUseFFTOceanNormalMap / gWaveGroupPhase / gFoamDriftOffsetXZ /
//             gSeabedEnabled / gSeabedOriginXZ / gSeabedInvSize / gShoreFoamEnabled
//   関数    : ComputeFFTCombinedDetJ（Common/FFTOceanCascade.hlsli）
// ============================================================
#ifndef WATER_FOAM_COVERAGE_INCLUDED
#define WATER_FOAM_COVERAGE_INCLUDED

#include "../Common/WaterWhitecapCoverage.hlsli"

// ---- 岸際泡（shore foam）----
// 範囲の外で泡帯が消える水深 [m]（水面の点の真下の鉛直水深）
static const float kShoreFoamDepthMeters = 0.6f;
// 汀線エッジ（この水深以浅）は被覆率満量＝ほぼ連続した白いシートになる
static const float kShoreFoamEdgeMeters = 0.15f;
// 外側の泡帯の最大被覆率。dissolve が形状を作るため 1 未満でもレース状に割れる
static const float kShoreFoamStrength = 0.9f;
// 海底の高さの範囲の端で、視線の延長で測った水深へ移す幅（範囲の一辺に対する割合）
static const float kSeabedWindowEdgeFade = 0.1f;

/// @brief 泡マスク [0,1] を求める（FFTOcean 専用）
/// @details 2 つの項の max で構成する:
///          - 瞬時項: 合成ヤコビアン detJ < gFoamBias（波頭の圧縮）。カスケード間の
///            強め合いを含む正確な砕波判定で、砕けている「今」を捉える。
///          - 蓄積項: FFTOceanFoamAccumulate.CS が時間発展させた泡。波が通過した
///            後に白い筋が数秒残る（発生時は瞬時項と同源なので max で二重計上しない）。
///          Gerstner 経路はヤコビアンを持たないため常に 0
///          （gUseFFTOceanNormalMap は FFT 使用フラグと同値で更新される）。
float ComputeFoamMask(float2 worldXZ)
{
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return 0.0f;
    }
    const float detJ = ComputeFFTCombinedDetJ(
        worldXZ, gFFTOceanJacobian, gSampler, gFoamCascadeWeights, gWaveGroupPhase);
    const float instant = ComputeWhitecapInstant(detJ, gFoamBias, gFoamGain);
    const float accumulated = SampleWhitecapAccumulated(
        worldXZ, gFFTOceanFoam, gSampler, gWaveGroupPhase, gFoamDriftOffsetXZ);

    // 返り値は滑らかな「被覆率」の場。レース状の形への変換（dissolve）は
    // 表示側の ComputeFoamLace が行うため、ここではノイズを掛けない。
    return saturate(max(instant, accumulated));
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
    const float2 uv = (surfacePosition.xz - gSeabedOriginXZ) * gSeabedInvSize;
    const float2 edgeDistance = min(uv, 1.0f - uv);
    const float inside = saturate(min(edgeDistance.x, edgeDistance.y) / kSeabedWindowEdgeFade);
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
    const float edge = 1.0f - smoothstep(0.0f, kShoreFoamEdgeMeters, verticalDepth);

    return max(band * kShoreFoamStrength, edge);
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

    const float2 uv = (surfacePosition.xz - gSeabedOriginXZ) * gSeabedInvSize;
    const float2 edgeDistance = min(uv, 1.0f - uv);
    const float inside = saturate(min(edgeDistance.x, edgeDistance.y) / kSeabedWindowEdgeFade);
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
    const float edge = 1.0f - smoothstep(0.0f, kShoreFoamEdgeMeters, verticalDepth);

    result.coverage = lerp(depthOnly, max(simulated, edge), inside);
    result.patternShift = (restXZ - surfacePosition.xz) * inside;
    return result;
}

#endif // WATER_FOAM_COVERAGE_INCLUDED

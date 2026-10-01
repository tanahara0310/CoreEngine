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
//   資源    : gFFTOceanJacobian / gFFTOceanFoam / gSampler / gWaterSeabedHeight / gLinearClamp
//   cbuffer : gFoamEnabled / gFoamBias / gFoamGain / gFoamCascadeWeights /
//             gFoamWindCoverageScale / gUseFFTOceanNormalMap /
//             gSeabedEnabled / gSeabedOriginXZ / gSeabedInvSize
//   関数    : ComputeFFTCombinedDetJ / ComputeFFTCascadeUV / ComputeFFTWaveGroupEnvelope
//             （Common/FFTOceanCascade.hlsli）
// ============================================================
#ifndef WATER_FOAM_COVERAGE_INCLUDED
#define WATER_FOAM_COVERAGE_INCLUDED

// 蓄積泡へ掛ける波群エンベロープの写像（瞬時項は合成 detJ 内で適用済み）。
// envelope² × この係数で、波群の強い所ほど泡が濃く、弱い所は薄くなる
static const float kFoamEnvelopeScale = 0.7f;

// ---- 岸際泡（shore foam）----
// 泡帯が消える水深 [m]（水面の点の真下の鉛直水深）。水深は波の変位を含むため、
// 波が寄せる/引くのに合わせて帯が自然に脈動する
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
        worldXZ, gFFTOceanJacobian, gSampler, gFoamCascadeWeights);
    const float instant = saturate((gFoamBias - detJ) * gFoamGain);

    // 蓄積泡（カスケード毎の格子空間）。重みは発生時に織り込み済みなのでそのまま max。
    float accumulated = 0.0f;
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        const float2 cuv = ComputeFFTCascadeUV(worldXZ, ci);
        accumulated = max(accumulated, gFFTOceanFoam.SampleLevel(gSampler, float3(cuv, (float)ci), 0.0f));
    }

    // 蓄積泡へ波群エンベロープを掛け、泡の濃淡を波のセット（うねりの群）と同期させる。
    // 瞬時項は合成 detJ の勾配へ適用済みなのでここでは掛けない（二重適用禁止）。
    // 蓄積パスは格子空間で走るためワールド位置を知らず、表示側で変調するしかない。
    const float envelope = ComputeFFTWaveGroupEnvelope(worldXZ);
    accumulated = saturate(accumulated * envelope * envelope * kFoamEnvelopeScale);

    // ★白波の量を風速へ追従させる★
    // detJ のしきい値は固定なので、これだけでは実海の風速依存
    // （Monahan: 白波被覆率 W ∝ U^3.41）に全く足りない。実測でも、高風速で
    // 合わせた設定のまま風速 4m/s にすると被覆率が実海推定の約 40 倍出ていた。
    // 被覆率へ Monahan 比を掛けることで、しきい値を触らずに風速追従させる
    // （dissolve のしきい値カットが効くため、マスクを下げると面積も減る）。
    // 岸際泡は砕波ではなく地形起因なので、ここでは掛けない（呼び出し側で max）。
    const float windScaledMask = max(instant, accumulated) * gFoamWindCoverageScale;

    // 返り値は滑らかな「被覆率」の場。レース状の形への変換（dissolve）は
    // 表示側の ComputeFoamLace が行うため、ここではノイズを掛けない。
    return saturate(windScaledMask);
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

/// @brief 岸際泡（shore foam）の被覆率 [0,1] を求める
/// @param verticalDepth 水面の点の真下の鉛直水深 [m]（ResolveShoreFoamDepth）
/// @details 水深は波の変位を含むため、波の寄せ引きで帯が自然に脈動する。
///          2 段構造: 汀線エッジ（〜0.15m）は被覆率満量＝連続した白いシート、
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

#endif // WATER_FOAM_COVERAGE_INCLUDED

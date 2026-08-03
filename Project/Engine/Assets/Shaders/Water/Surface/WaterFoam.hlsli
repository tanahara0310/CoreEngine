// ============================================================
// 泡（whitecap / shore foam）シェーディング（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 泡は水そのものの色ではなく、砕波で空気が混入した「白い散乱層」。ほぼ Lambert な
// 拡散面として扱い、フレネル反射・鏡面はほぼ持たないため、合成時に
// reflectanceWeight とサングリッターを (1 - 泡被覆) 倍へ抑制する。
//
// 【飽和対策 3 点セットの一角】瞬時マスクはカスケード重み付き合成 detJ
// （ComputeFFTCombinedDetJ）を必ず通すこと。波群エンベロープは蓄積項のみに掛ける
// （瞬時項は合成 detJ 内で適用済み＝二重適用禁止）。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterFrameConstants(b5)・
// WaterVolume.hlsli（EvaluateWaterSkyIrradiance）の後で include すること。以下に暗黙依存:
//   資源    : gFFTOceanJacobian / gFFTOceanFoam / gSampler / gIrradianceMap /
//             gLightCounts / gDirectionalLights / gIBLParams（Object3dForward.hlsli）
//   cbuffer : gFoamEnabled / gFoamBias / gFoamGain / gFoamOpacity /
//             gFoamCascadeWeights / gUseFFTOceanNormalMap / gSkyAmbientEnabled / gSkyAmbientScale
//   関数    : ComputeFFTCombinedDetJ / ComputeFFTCascadeUV / ComputeFFTWaveGroupEnvelope
//             （Common/FFTOceanCascade.hlsli）・EvaluateWaterSkyIrradiance（WaterVolume.hlsli）
// ============================================================
#ifndef WATER_FOAM_INCLUDED
#define WATER_FOAM_INCLUDED

// アルベドはわずかに青白い（気泡層の多重散乱による短波長優位）。
static const float3 kFoamAlbedo = float3(0.90f, 0.93f, 0.95f);

// ---- 泡の描画方式: dissolve（しきい値カット）----
// 泡マスクは「被覆率」の滑らかな場として持ち、表示時に高周波の泡パターンへ
// しきい値カットを掛けて「泡がある/ない」のレース状の形へ変換する。
// 被覆率をそのまま明度にすると（旧方式）、泡が「ぼかしたノイズテクスチャ」に
// 見えてしまう — 実際の泡はレースの穴・筋・粒という鋭い微細構造を持つため。
// dissolve のしきい値の柔らかさ（上側だけ滑らか。下端は硬い＝縁が鋭い）
static const float kFoamLaceSoftness = 0.18f;
// 白濁（haze）: 泡レースの穴の間と縁の外側に出る気泡層。パターンで粒状に変調し、
// 「均一な白いもや」に見えないようにする
static const float kFoamHazeTint = 0.55f;      // 泡色に対する輝度比（水越しの気泡層）
static const float kFoamHazeOpacity = 0.35f;   // 最大ブレンド率（旧 0.5 から抑制）
static const float kFoamHazePatternMin = 0.25f; // パターン変調の下限（0=完全に粒状）
// 泡内部の粒状の明度変調（気泡の粒感。周期 ≈ 8cm）
static const float kFoamGrainScale = 13.0f;
static const float kFoamGrainMin = 0.82f;
// 蓄積泡へ掛ける波群エンベロープの写像（瞬時項は合成 detJ 内で適用済み）。
// envelope² × この係数で、波群の強い所ほど泡が濃く、弱い所は薄くなる
static const float kFoamEnvelopeScale = 0.7f;
// 泡域でのグリッター用ラフネス（泡は微細気泡でハイライトが大きく柔らかくなる）
static const float kFoamGlintRoughness = 0.45f;

// ---- 岸際泡（shore foam）----
// 泡帯が消える水深 [m]。解析的鉛直水深は波の変位を含むため、
// 波が寄せる/引くのに合わせて帯が自然に脈動する
static const float kShoreFoamDepthMeters = 0.6f;
// 汀線エッジ（この水深以浅）は被覆率満量＝ほぼ連続した白いシートになる
static const float kShoreFoamEdgeMeters = 0.15f;
// 外側の泡帯の最大被覆率。dissolve が形状を作るため 1 未満でもレース状に割れる
static const float kShoreFoamStrength = 0.9f;

/// @brief 泡分断用の 2D ハッシュ（[0,1]）
float FoamHash(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

/// @brief 泡分断用の value noise（[0,1]・C1 連続）
float FoamValueNoise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    const float a = FoamHash(i);
    const float b = FoamHash(i + float2(1.0f, 0.0f));
    const float c = FoamHash(i + float2(0.0f, 1.0f));
    const float d = FoamHash(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

/// @brief 泡の内部パターン [0,1]（dissolve のしきい値場）
/// @details fbm 4 オクターブ（周期 ≈ 1.5m / 0.6m / 0.25m / 0.1m）＋ ridged 成分
///          （筋・網目）の合成。蓄積泡テクスチャは最大 2m/テクセルしかないため、
///          近景の泡ディテールはすべてこのパターンが担う（テクスチャ資産は不要）。
///          泡マスク自体が波と一緒に動くため、パターンはワールド固定でも
///          「泡の中の模様」として自然に見える。
float FoamPattern(float2 worldXZ)
{
    const float fbm = FoamValueNoise(worldXZ * 0.65f) * 0.40f
                    + FoamValueNoise(worldXZ * 1.7f) * 0.25f
                    + FoamValueNoise(worldXZ * 4.1f) * 0.20f
                    + FoamValueNoise(worldXZ * 9.7f) * 0.15f;
    // ridged: 谷折りの筋（1-|2n-1|）を二乗して鋭く。レースの網目構造を作る
    float ridge = 1.0f - abs(FoamValueNoise(worldXZ * 1.1f) * 2.0f - 1.0f);
    ridge *= ridge;
    return saturate(fbm * 0.65f + ridge * 0.35f);
}

/// @brief 被覆率マスク → レース状の泡形状 [0,1]（dissolve カット）
/// @details パターンがしきい値 (1-mask) を超えた場所が泡になる。
///          mask=0 でしきい値 1（泡なし）、mask=1 でしきい値 0（全て埋まる）。
///          下端が硬い smoothstep なので縁は常に鋭いレース状になり、
///          被覆率が上がるとパターンの高い所から順に泡が「埋まって」いく。
float ComputeFoamLace(float mask, float pattern)
{
    return smoothstep(1.0f - mask, 1.0f - mask + kFoamLaceSoftness, pattern);
}

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

    // 返り値は滑らかな「被覆率」の場。レース状の形への変換（dissolve）は
    // 表示側の ComputeFoamLace が行うため、ここではノイズを掛けない。
    return max(instant, accumulated);
}

/// @brief 岸際泡（shore foam）の被覆率 [0,1] を求める
/// @param analyticColumn 解析的な鉛直水深 [m]（ResolveWaterColumn の連続場）
/// @details ★入力は解析的鉛直水深のみ★
///          過去の「波打ち際の線」10 連発の教訓により、岸際に新しい 2 値切替を
///          持ち込まない。RT の成功/失敗・スクリーン空間の深度分岐などの離散量は
///          一切使わず、分岐のない連続場（解析水深）だけから作る。
///          水深は波の変位を含むため、波の寄せ引きで帯が自然に脈動し、
///          追加の時間変調は不要。
///          2 段構造: 汀線エッジ（〜0.15m）は被覆率満量＝連続した白いシート、
///          外側（〜0.6m）は被覆率 0.9→0 のフェード＝dissolve でレース状に割れる。
float ComputeShoreFoamMask(float analyticColumn)
{
    if (gFoamEnabled == 0 || gUseFFTOceanNormalMap == 0)
    {
        return 0.0f;
    }

    // 外側の泡帯: 水深 0 → kShoreFoamDepthMeters の連続フェード
    const float band = 1.0f - smoothstep(0.0f, kShoreFoamDepthMeters, analyticColumn);
    // 汀線エッジ: ごく浅い所は満量（レースの穴が埋まり白いシートになる）
    const float edge = 1.0f - smoothstep(0.0f, kShoreFoamEdgeMeters, analyticColumn);

    return max(band * kShoreFoamStrength, edge);
}

/// @brief 泡レイヤの表面色（Lambert 白 × 太陽直達 + 天空光）
/// @details 天空光は水中インスキャッタ（ComputeUnderwaterAmbientLight）と同じ
///          ソース・同じスケールを使い、空との明るさを常に整合させる。
///          太陽ライトの色には大気の Transmittance 減衰が乗算済みなので、
///          日没時は泡も自動的に赤みを帯びて暗くなる。
float3 ComputeFoamColor(float3 normal)
{
    // 平行光源（太陽・月）の直達成分: E·NdotL / π
    float3 lighting = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float3 lightVec = -normalize(gDirectionalLights[i].direction);
        lighting += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity
            * saturate(dot(normal, lightVec)) / PI;
    }

    // 天空光（大気アクティブ時は Sky Irradiance SH、なければ静的 IBL へフォールバック）
    if (gSkyAmbientEnabled != 0)
    {
        lighting += EvaluateWaterSkyIrradiance(normal) * gSkyAmbientScale;
    }
    else if (gIBLParams.sceneIBLEnabled != 0)
    {
        lighting += gIrradianceMap.SampleLevel(gSampler, normal, 0.0f).rgb
            * gIBLParams.environmentIntensity;
    }

    return kFoamAlbedo * lighting;
}

#endif // WATER_FOAM_INCLUDED

// ============================================================
// 泡の見た目（模様・形・色）（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 被覆率（WaterFoamCoverage.hlsli）を、レース状の泡の形・水面下の白濁・泡の色へ変える。
// 泡は水そのものの色ではなく、砕波で空気が混入した「白い散乱層」。ほぼ Lambert な
// 拡散面として扱い、フレネル反射・鏡面はほぼ持たないため、合成時に
// reflectanceWeight とサングリッターを (1 - 泡被覆) 倍へ抑制する。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterFrameConstants(b5)・
// WaterVolume.hlsli（EvaluateWaterSkyIrradiance）の後で include すること。以下に暗黙依存:
//   資源    : gSampler / gIrradianceMap / gLightCounts / gDirectionalLights /
//             gIBLParams（Object3dForward.hlsli）
//   cbuffer : gSkyAmbientEnabled / gSkyAmbientScale / gFoamDriftOffsetXZ / gFoamStretchAxis
//   関数    : EvaluateWaterSkyIrradiance（WaterVolume.hlsli）
// ============================================================
#ifndef WATER_FOAM_APPEARANCE_INCLUDED
#define WATER_FOAM_APPEARANCE_INCLUDED

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
// 泡域でのグリッター用ラフネス（泡は微細気泡でハイライトが大きく柔らかくなる）
static const float kFoamGlintRoughness = 0.45f;

/// @brief 泡分断用の 2D ハッシュ（[0,1)）
/// @param p 格子点（整数値の float2）
/// @details 格子点の整数座標から整数演算で作るので、原点から遠くても値の分布が変わらない。
///          Tools/Water/generate_foam_coverage_table.py が同じ式を使う
float FoamHash(float2 p)
{
    const uint2 q = asuint(int2(p));
    uint h = (q.x * 0x8da6b343u) ^ (q.y * 0xd8163841u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
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

/// @brief 被覆率 c に対する dissolve のマスク。レースの面積の期待値がちょうど c になる
/// @details √c の等間隔（c = (k / 32)²）で並べる。Tools/Water/generate_foam_coverage_table.py が
///          FoamPattern の値の分布から作る。
/// @warning FoamPattern か kFoamLaceSoftness を変えたら作り直すこと
static const float kFoamLaceMaskForCoverage[33] = {
    0.0000f, 0.2371f, 0.2765f, 0.3043f, 0.3269f, 0.3465f, 0.3642f, 0.3806f,
    0.3961f, 0.4108f, 0.4251f, 0.4390f, 0.4527f, 0.4663f, 0.4798f, 0.4933f,
    0.5069f, 0.5206f, 0.5345f, 0.5486f, 0.5631f, 0.5779f, 0.5931f, 0.6089f,
    0.6254f, 0.6426f, 0.6609f, 0.6806f, 0.7024f, 0.7273f, 0.7578f, 0.8007f,
    1.1170f,
};

/// @brief 被覆率 [0,1] を、レースの面積がその割合になる dissolve のマスクへ変える
float FoamLaceMaskForCoverage(float coverage)
{
    const float x = sqrt(saturate(coverage)) * 32.0f;
    const uint i = min((uint)x, 31u);
    return lerp(kFoamLaceMaskForCoverage[i], kFoamLaceMaskForCoverage[i + 1], x - (float)i);
}

/// @brief 水面の 1 点の泡の見た目の割合
struct WaterFoamLayer
{
    float lace;    ///< 水面の上の白い泡（レース）[0,1]
    float haze;    ///< レースの穴の間と縁の外側の白濁 [0,1]
    float pattern; ///< 泡の内部パターン [0,1]
};

/// @brief 被覆率から、その点の泡のレースと白濁の割合を求める
/// @param mask     泡の被覆率 [0,1]（水面のうち泡のレースが占める面積の割合）
/// @param patternXZ 模様を評価する位置（泡の塊と一緒に運ばれる座標）
WaterFoamLayer EvaluateFoamLayer(float mask, float2 patternXZ)
{
    WaterFoamLayer layer;
    layer.pattern = FoamPattern(patternXZ);
    layer.lace = ComputeFoamLace(FoamLaceMaskForCoverage(mask), layer.pattern);
    layer.haze = saturate(mask * 1.2f) * (1.0f - layer.lace)
        * lerp(kFoamHazePatternMin, 1.0f, layer.pattern);
    return layer;
}

/// @brief 白波の泡の模様を評価する位置（泡と一緒に風下へ流し、風の向きに伸ばす）
/// @param baseWorldXZ 変位前の参照格子座標
/// @details 風の向きの成分だけを 1/伸び率 倍に縮める。座標の一次変換なので、模様の値の分布
///          （kFoamLaceMaskForCoverage の前提）は変わらない
float2 ComputeWhitecapPatternXZ(float2 baseWorldXZ)
{
    const float2 driftedXZ = baseWorldXZ - gFoamDriftOffsetXZ;
    return driftedXZ - gFoamStretchAxis * dot(gFoamStretchAxis, driftedXZ);
}

/// @brief 2 つの泡の層を重ねる（レースは和集合。白濁は相手のレースの下に隠れる）
WaterFoamLayer CombineFoamLayers(WaterFoamLayer a, WaterFoamLayer b)
{
    WaterFoamLayer layer;
    layer.lace = 1.0f - (1.0f - a.lace) * (1.0f - b.lace);
    layer.haze = max(a.haze * (1.0f - b.lace), b.haze * (1.0f - a.lace));
    layer.pattern = max(a.pattern, b.pattern);
    return layer;
}

/// @brief 泡の内部の粒状の明度変調 [kFoamGrainMin, 1]
float FoamGrain(float2 baseWorldXZ)
{
    return lerp(kFoamGrainMin, 1.0f, FoamValueNoise(baseWorldXZ * kFoamGrainScale));
}

/// @brief 泡レイヤの表面色（Lambert 白 × 太陽直達 + 天空光）
/// @details 泡は水面の上の白い面なので、天空光は地面の環境光と同じく
///          Sky Irradiance SH に gSkyAmbientScale を掛けて使う。
///          太陽ライトの色には大気の Transmittance 減衰が乗算済みなので、
///          日没時は泡も自動的に赤みを帯びて暗くなる。
/// @param mainLightVisibility メインライト（0 番）の日向率（0=影 / 1=日向）
float3 ComputeFoamColor(float3 normal, float mainLightVisibility)
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
        const float visibility = (i == 0) ? mainLightVisibility : 1.0f;
        lighting += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity
            * saturate(dot(normal, lightVec)) * visibility / PI;
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

#endif // WATER_FOAM_APPEARANCE_INCLUDED

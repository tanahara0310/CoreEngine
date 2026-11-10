// ============================================================
// 泡の見た目（模様・形・色）（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 被覆率（WaterFoamCoverage.hlsli）を、レース状の泡の形・水面下の白濁・泡の色へ変える。
// 泡は水そのものの色ではなく、砕波で空気が混入した「白い散乱層」。ほぼ Lambert な
// 拡散面として扱い、フレネル反射・鏡面はほぼ持たないため、合成時に
// reflectanceWeight とサングリッターを (1 - 泡被覆) 倍へ抑制する。
// ============================================================
#ifndef WATER_FOAM_APPEARANCE_INCLUDED
#define WATER_FOAM_APPEARANCE_INCLUDED

#include "WaterSurfaceResources.hlsli"
#include "WaterFoamCoverage.hlsli"
#include "WaterVolume.hlsli" // EvaluateWaterSkyIrradiance

// アルベドはわずかに青白い（気泡層の多重散乱による短波長優位）。
static const float3 kFoamAlbedo = float3(0.90f, 0.93f, 0.95f);

// ---- 泡の描画方式: dissolve（しきい値カット）----
// 泡マスクは「被覆率」の滑らかな場として持ち、表示時に泡の膜の網目の模様へ
// しきい値カットを掛けて「泡がある/ない」のレース状の形へ変換する。
// dissolve のしきい値の柔らかさ（しきい値の前後に半分ずつ）
static const float kFoamLaceSoftness = 0.08f;
// 白濁（haze）: 砕けた直後の泡の穴の間と縁の外側に出る気泡層。パターンで粒状に変調し、
// 「均一な白いもや」に見えないようにする
static const float kFoamHazeTint = 0.55f;      // 泡色に対する輝度比（水越しの気泡層）
static const float kFoamHazeOpacity = 0.35f;   // 最大ブレンド率
static const float kFoamHazePatternMin = 0.25f; // パターン変調の下限（0=完全に粒状）
// 砕けてから時間が経った薄い泡の、レースの不透明度の割合（砕けた直後を 1 とする）
static const float kFoamAgedOpacity = 0.6f;
// 泡内部の粒状の明度変調（気泡の粒感。周期 ≈ 8cm）
static const float kFoamGrainScale = 13.0f;
static const float kFoamGrainMin = 0.82f;
// 粒の明度変調を平均へ移し始める・移し終える 1 画素の幅 [m]
static const float kFoamGrainFadeStart = 0.03f;
static const float kFoamGrainFadeEnd = 0.08f;
// 泡域でのグリッター用ラフネス（泡は微細気泡でハイライトが大きく柔らかくなる）
static const float kFoamGlintRoughness = 0.45f;

// ---- 泡の模様（泡の膜が割れて残る網目）----
// 大きさの違う 3 段のボロノイの境目（0 = 大きい網目・1 = 中・2 = 細かい）を重ね、
// 数 m の大きさの塊の場で濃く出る所と抜ける所を分ける。網目の点は時間でゆっくり動く。
// Tools/Water/generate_foam_coverage_table.py が同じ式で面積の表と平均を作る（値を変えたら作り直す）
/// @brief 模様を動かす時刻の周期 [s]（WaterWhitecapFoam::kPatternPeriodSeconds と同じ値）
static const float kFoamPatternPeriodSeconds = 600.0f;
static const float kFoamEdgeCellSizes[3] = { 1.3f, 0.45f, 0.15f };     // 網目の 1 マスの大きさ [m]
static const float kFoamEdgeAngles[3] = { 0.43f, 1.91f, 3.07f };       // 段ごとの回転 [rad]
static const float kFoamEdgeWidths[3] = { 0.28f, 0.34f, 0.42f };       // 境目の線の幅（マスの大きさに対する割合）
static const float kFoamEdgeWeights[3] = { 0.58f, 0.28f, 0.14f };      // 網目の値に足す重み
static const float kFoamEdgeStrengthMin[3] = { 0.30f, 0.55f, 0.80f };  // 境目ごとの強さの下限（弱い境目から先に消える）
static const float kFoamEdgeWarps[3] = { 0.7f, 0.4f, 0.0f };           // 網目をゆがめる量（マスの大きさに対する割合）
static const float kFoamEdgeWarpFrequencies[3] = { 0.4f, 1.2f, 0.0f }; // ゆがみの細かさ [1/m]
static const uint kFoamEdgeSeeds[3] = { 11u, 23u, 37u };
// 段ごとの値の傾きの大きさの目安 [1/m]（重み × 塊の場の上限 × 線の立ち上がり ÷ 線の幅）
static const float kFoamEdgeSlopes[3] = { 2.15f, 2.47f, 3.0f };
// 網目の点の置き方（マスの中の散らばりと、時間で動く幅。どちらもマスの大きさに対する割合）
static const float kFoamCellJitter = 0.62f;
static const float kFoamCellMotion = 0.12f;
// 段を平均へ移し始める・移し終える 1 画素の幅（マスの大きさに対する割合）
static const float kFoamEdgeFadeStart = 0.15f;
static const float kFoamEdgeFadeEnd = 0.40f;
// dissolve のしきい値の前後の幅の上限
static const float kFoamMaxEdgeWidth = 0.1f;
// 模様をやめて被覆率の平均へ移し始める・移し終える 1 画素の幅 [m]
static const float kFoamFlattenStart = 0.6f;
static const float kFoamFlattenEnd = 1.6f;
// これより薄い泡は模様を使わず被覆率で塗る
static const float kFoamPatternMinCoverage = 0.004f;

// 段ごとの値の平均（遠くでその段を平均へ移すときに使う）
static const float kFoamEdgeMeans[3] = { 0.1870f, 0.2643f, 0.3673f };
// 模様の値の平均（模様を評価しない遠くで、白濁の変調に使う）
static const float kFoamPatternMean = 0.2786f;
/// @brief 被覆率 c に対する dissolve のマスク。レースの面積の期待値がちょうど c になる
/// @details 行は平均へ移した段の数の 0.25 刻み（0 行目 = どの段も模様のまま・12 行目 = 3 段とも平均）。
///          列は √c の等間隔（c = (k / 32)²）
static const float kFoamLaceMaskForCoverage[13][33] = {
    {
        0.0000f, 0.1578f, 0.2514f, 0.3148f, 0.3650f, 0.4075f, 0.4444f, 0.4770f,
        0.5061f, 0.5328f, 0.5575f, 0.5806f, 0.6022f, 0.6225f, 0.6417f, 0.6601f,
        0.6777f, 0.6947f, 0.7113f, 0.7273f, 0.7430f, 0.7585f, 0.7738f, 0.7892f,
        0.8048f, 0.8208f, 0.8374f, 0.8550f, 0.8740f, 0.8954f, 0.9209f, 0.9554f,
        1.1210f,
    },
    {
        0.0000f, 0.1633f, 0.2550f, 0.3177f, 0.3674f, 0.4097f, 0.4466f, 0.4791f,
        0.5081f, 0.5346f, 0.5591f, 0.5820f, 0.6035f, 0.6237f, 0.6429f, 0.6612f,
        0.6788f, 0.6957f, 0.7120f, 0.7279f, 0.7434f, 0.7586f, 0.7737f, 0.7889f,
        0.8043f, 0.8200f, 0.8365f, 0.8539f, 0.8728f, 0.8941f, 0.9196f, 0.9542f,
        1.1184f,
    },
    {
        0.0000f, 0.1673f, 0.2577f, 0.3197f, 0.3692f, 0.4113f, 0.4482f, 0.4806f,
        0.5095f, 0.5359f, 0.5602f, 0.5830f, 0.6043f, 0.6245f, 0.6438f, 0.6621f,
        0.6796f, 0.6964f, 0.7126f, 0.7283f, 0.7436f, 0.7587f, 0.7737f, 0.7886f,
        0.8038f, 0.8195f, 0.8358f, 0.8531f, 0.8719f, 0.8932f, 0.9187f, 0.9532f,
        1.1159f,
    },
    {
        0.0000f, 0.1698f, 0.2592f, 0.3209f, 0.3702f, 0.4123f, 0.4491f, 0.4815f,
        0.5104f, 0.5367f, 0.5609f, 0.5836f, 0.6048f, 0.6250f, 0.6443f, 0.6627f,
        0.6802f, 0.6969f, 0.7129f, 0.7285f, 0.7437f, 0.7588f, 0.7736f, 0.7885f,
        0.8036f, 0.8191f, 0.8354f, 0.8526f, 0.8714f, 0.8926f, 0.9181f, 0.9526f,
        1.1133f,
    },
    {
        0.0000f, 0.1706f, 0.2596f, 0.3212f, 0.3705f, 0.4127f, 0.4493f, 0.4818f,
        0.5108f, 0.5370f, 0.5612f, 0.5838f, 0.6050f, 0.6251f, 0.6444f, 0.6629f,
        0.6804f, 0.6970f, 0.7130f, 0.7286f, 0.7438f, 0.7588f, 0.7736f, 0.7885f,
        0.8035f, 0.8190f, 0.8352f, 0.8524f, 0.8712f, 0.8924f, 0.9179f, 0.9525f,
        1.1107f,
    },
    {
        0.0000f, 0.1905f, 0.2723f, 0.3311f, 0.3789f, 0.4195f, 0.4554f, 0.4876f,
        0.5167f, 0.5431f, 0.5673f, 0.5898f, 0.6109f, 0.6307f, 0.6494f, 0.6669f,
        0.6833f, 0.6990f, 0.7141f, 0.7289f, 0.7434f, 0.7577f, 0.7720f, 0.7864f,
        0.8011f, 0.8163f, 0.8322f, 0.8493f, 0.8679f, 0.8891f, 0.9146f, 0.9494f,
        1.1070f,
    },
    {
        0.0000f, 0.2044f, 0.2812f, 0.3381f, 0.3849f, 0.4244f, 0.4595f, 0.4912f,
        0.5203f, 0.5473f, 0.5720f, 0.5947f, 0.6158f, 0.6353f, 0.6531f, 0.6697f,
        0.6854f, 0.7004f, 0.7150f, 0.7292f, 0.7431f, 0.7570f, 0.7708f, 0.7849f,
        0.7993f, 0.8143f, 0.8300f, 0.8469f, 0.8655f, 0.8866f, 0.9121f, 0.9470f,
        1.1033f,
    },
    {
        0.0000f, 0.2122f, 0.2863f, 0.3423f, 0.3883f, 0.4274f, 0.4619f, 0.4932f,
        0.5221f, 0.5493f, 0.5748f, 0.5982f, 0.6193f, 0.6381f, 0.6553f, 0.6714f,
        0.6868f, 0.7015f, 0.7156f, 0.7294f, 0.7430f, 0.7565f, 0.7702f, 0.7840f,
        0.7983f, 0.8131f, 0.8287f, 0.8455f, 0.8640f, 0.8850f, 0.9105f, 0.9455f,
        1.0996f,
    },
    {
        0.0000f, 0.2141f, 0.2878f, 0.3436f, 0.3894f, 0.4285f, 0.4627f, 0.4938f,
        0.5227f, 0.5498f, 0.5755f, 0.5997f, 0.6205f, 0.6390f, 0.6560f, 0.6720f,
        0.6873f, 0.7019f, 0.7159f, 0.7295f, 0.7430f, 0.7564f, 0.7699f, 0.7837f,
        0.7979f, 0.8127f, 0.8283f, 0.8450f, 0.8634f, 0.8845f, 0.9099f, 0.9449f,
        1.0959f,
    },
    {
        0.0000f, 0.2973f, 0.3587f, 0.4058f, 0.4440f, 0.4766f, 0.5053f, 0.5316f,
        0.5560f, 0.5779f, 0.5967f, 0.6136f, 0.6292f, 0.6442f, 0.6586f, 0.6724f,
        0.6857f, 0.6987f, 0.7116f, 0.7243f, 0.7370f, 0.7499f, 0.7630f, 0.7765f,
        0.7904f, 0.8051f, 0.8206f, 0.8374f, 0.8559f, 0.8772f, 0.9031f, 0.9391f,
        1.0905f,
    },
    {
        0.0000f, 0.3785f, 0.4268f, 0.4641f, 0.4944f, 0.5205f, 0.5436f, 0.5626f,
        0.5789f, 0.5939f, 0.6080f, 0.6217f, 0.6350f, 0.6477f, 0.6602f, 0.6724f,
        0.6845f, 0.6965f, 0.7084f, 0.7204f, 0.7326f, 0.7450f, 0.7578f, 0.7710f,
        0.7847f, 0.7992f, 0.8146f, 0.8314f, 0.8500f, 0.8714f, 0.8976f, 0.9343f,
        1.0851f,
    },
    {
        0.0000f, 0.4552f, 0.4878f, 0.5137f, 0.5331f, 0.5488f, 0.5631f, 0.5766f,
        0.5897f, 0.6026f, 0.6149f, 0.6269f, 0.6385f, 0.6500f, 0.6614f, 0.6726f,
        0.6839f, 0.6952f, 0.7066f, 0.7182f, 0.7300f, 0.7421f, 0.7546f, 0.7676f,
        0.7811f, 0.7954f, 0.8108f, 0.8274f, 0.8460f, 0.8674f, 0.8938f, 0.9309f,
        1.0796f,
    },
    {
        0.0000f, 0.5033f, 0.5166f, 0.5295f, 0.5422f, 0.5549f, 0.5680f, 0.5812f,
        0.5938f, 0.6058f, 0.6175f, 0.6289f, 0.6400f, 0.6510f, 0.6620f, 0.6729f,
        0.6838f, 0.6949f, 0.7061f, 0.7175f, 0.7292f, 0.7411f, 0.7535f, 0.7664f,
        0.7798f, 0.7941f, 0.8094f, 0.8260f, 0.8445f, 0.8660f, 0.8923f, 0.9295f,
        1.0742f,
    },
};

/// @brief 網目の点の乱数に使う 3 成分のハッシュ
uint3 FoamPcg3(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

/// @brief マスごとの 3 つの乱数 [0,1)
float3 FoamHash3(int2 cell, uint seed)
{
    return (float3)(FoamPcg3(uint3(asuint(cell), seed)) >> 8u) * (1.0f / 16777216.0f);
}

/// @brief マスを見分ける整数
uint FoamCellKey(int2 cell)
{
    const uint2 q = asuint(cell);
    return (q.x * 0x8da6b343u) ^ (q.y * 0xd8163841u);
}

/// @brief ボロノイの境目までの近さと、その境目の強さ
/// @param p    マスの大きさを 1 にした座標
/// @param time 模様を動かす時刻 [s]
/// @return x = 2 番目に近い点と最も近い点の距離の差（境目の上で 0）・y = 境目ごとの乱数 [0,1)
float2 FoamCellEdge(float2 p, float time, uint seed)
{
    const float2 cellFloor = floor(p);
    const int2 cell = int2(cellFloor);
    const float2 f = p - cellFloor;
    const float jitterLow = 0.5f - 0.5f * kFoamCellJitter;
    float d1 = 8.0f;
    float d2 = 8.0f;
    uint key1 = 0u;
    uint key2 = 0u;
    [unroll]
    for (int j = -1; j <= 1; ++j)
    {
        [unroll]
        for (int i = -1; i <= 1; ++i)
        {
            const int2 neighbor = cell + int2(i, j);
            const float3 h = FoamHash3(neighbor, seed);
            const float speed = floor(4.0f + h.z * 8.0f);
            const float phase = TWO_PI * (speed * time / kFoamPatternPeriodSeconds + h.z);
            const float2 cellPoint = float2((float)i, (float)j) + jitterLow + kFoamCellJitter * h.xy
                + kFoamCellMotion * float2(sin(phase), sin(phase + 1.9f));
            const float d = length(cellPoint - f);
            const uint key = FoamCellKey(neighbor);
            if (d < d1)
            {
                d2 = d1;
                key2 = key1;
                d1 = d;
                key1 = key;
            }
            else if (d < d2)
            {
                d2 = d;
                key2 = key;
            }
        }
    }
    const uint edgeHash = FoamPcg3(uint3(min(key1, key2), max(key1, key2), seed + 7u)).x;
    return float2(d2 - d1, (float)(edgeHash >> 8u) * (1.0f / 16777216.0f));
}

/// @brief 1 段の網目の線の値 [0,1]（境目の上で境目の強さ、境目から離れると 0）
float FoamEdgeLine(float2 xz, float time, uint octave)
{
    const float cellSize = kFoamEdgeCellSizes[octave];
    float s;
    float c;
    sincos(kFoamEdgeAngles[octave], s, c);
    float2 p = float2(c * xz.x - s * xz.y, s * xz.x + c * xz.y);
    const float warp = kFoamEdgeWarps[octave];
    if (warp > 0.0f)
    {
        const float frequency = kFoamEdgeWarpFrequencies[octave];
        const float2 offset = float2(
            FoamValueNoise(xz * frequency + float2(17.3f, -41.1f)),
            FoamValueNoise(xz * frequency + float2(-63.7f, 29.9f))) * 2.0f - 1.0f;
        p += (warp * cellSize) * offset;
    }
    const float2 edge = FoamCellEdge(p / cellSize, time, kFoamEdgeSeeds[octave]);
    float edgeLine = saturate(1.0f - edge.x / kFoamEdgeWidths[octave]);
    edgeLine = edgeLine * edgeLine * (3.0f - 2.0f * edgeLine);
    return edgeLine * lerp(kFoamEdgeStrengthMin[octave], 1.0f, edge.y);
}

/// @brief 泡の塊の場 [0,1]（網目が濃く出る所と抜ける所を数 m の大きさで分ける）
float FoamClusterField(float2 xz)
{
    const float noise = FoamValueNoise(xz * 0.22f + float2(3.1f, 7.7f)) * 0.55f
                      + FoamValueNoise(xz * 0.55f + float2(-5.3f, 1.9f)) * 0.30f
                      + FoamValueNoise(xz * 1.4f + float2(9.1f, 2.2f)) * 0.15f;
    return saturate((noise - 0.2f) / 0.6f);
}

/// @brief 泡の模様 [0,1]（dissolve のしきい値の場。値の大きい所から泡になる）
/// @param xz         模様を評価する位置 [m]
/// @param time       模様を動かす時刻 [s]
/// @param octaveFade 段ごとに平均へ移す割合（1 の段は評価しない）
float FoamPattern(float2 xz, float time, float3 octaveFade)
{
    float net = 0.0f;
    [unroll]
    for (uint octave = 0; octave < 3; ++octave)
    {
        float value = kFoamEdgeMeans[octave];
        [branch]
        if (octaveFade[octave] < 1.0f)
        {
            value = lerp(FoamEdgeLine(xz, time, octave), value, octaveFade[octave]);
        }
        net += kFoamEdgeWeights[octave] * value;
    }
    const float cluster = FoamClusterField(xz);
    return saturate(cluster * (0.3f + 0.7f * net) + 0.2f * net);
}

/// @brief 1 画素の大きさに合わせた泡の模様のぼかし方
struct FoamPatternFilter
{
    float3 octaveFade; ///< 段ごとに平均へ移す割合（細かい段から 1 になる）
    float tableLevel;  ///< 面積の表の段（平均へ移した段の数 [0,3]）
    float edgeWidth;   ///< dissolve のしきい値の前後の幅
    float flatten;     ///< 模様をやめて被覆率の平均へ移す割合
    float grainFade;   ///< 粒の明度変調を平均へ移す割合
};

/// @brief 水面の 1 画素の大きさから、泡の模様のぼかし方を求める
/// @param baseWorldXZ 変位前の参照格子座標（画素ごとの差分で 1 画素の大きさを測る）
/// @details 画素より細かくなる段から平均へ移し、dissolve の幅は 1 画素の模様の変化の半分を下限にする
FoamPatternFilter ComputeFoamPatternFilter(float2 baseWorldXZ)
{
    const float footprint = max(length(ddx(baseWorldXZ)), length(ddy(baseWorldXZ)));
    FoamPatternFilter filter;
    float slope = 0.0f;
    [unroll]
    for (uint octave = 0; octave < 3; ++octave)
    {
        const float relative = footprint / kFoamEdgeCellSizes[octave];
        filter.octaveFade[octave] =
            saturate((relative - kFoamEdgeFadeStart) / (kFoamEdgeFadeEnd - kFoamEdgeFadeStart));
        slope += (1.0f - filter.octaveFade[octave]) * kFoamEdgeSlopes[octave];
    }
    filter.tableLevel = filter.octaveFade.x + filter.octaveFade.y + filter.octaveFade.z;
    filter.edgeWidth = clamp(0.5f * slope * footprint, 0.5f * kFoamLaceSoftness, kFoamMaxEdgeWidth);
    filter.flatten = saturate((footprint - kFoamFlattenStart) / (kFoamFlattenEnd - kFoamFlattenStart));
    filter.grainFade = saturate((footprint - kFoamGrainFadeStart) / (kFoamGrainFadeEnd - kFoamGrainFadeStart));
    return filter;
}

/// @brief 被覆率マスク → レース状の泡形状 [0,1]（dissolve カット）
/// @details 模様がしきい値 (1-mask) を超えた場所が泡になる。しきい値の前後 edgeWidth で 0 から 1 へ上がる。
///          被覆率が上がると模様の高い所から順に泡が「埋まって」いく。
float ComputeFoamLace(float mask, float pattern, float edgeWidth)
{
    const float center = 1.0f - mask + 0.5f * kFoamLaceSoftness;
    return smoothstep(center - edgeWidth, center + edgeWidth, pattern);
}

/// @brief 被覆率 [0,1] を、レースの面積がその割合になる dissolve のマスクへ変える
/// @param tableLevel 面積の表の段（FoamPatternFilter::tableLevel）
float FoamLaceMaskForCoverage(float coverage, float tableLevel)
{
    const float x = sqrt(saturate(coverage)) * 32.0f;
    const uint i = min((uint)x, 31u);
    const float t = x - (float)i;
    const float rowPosition = tableLevel * 4.0f;
    const uint row = min((uint)rowPosition, 11u);
    const float rowT = saturate(rowPosition - (float)row);
    const float a = lerp(kFoamLaceMaskForCoverage[row][i], kFoamLaceMaskForCoverage[row][i + 1], t);
    const float b = lerp(kFoamLaceMaskForCoverage[row + 1][i], kFoamLaceMaskForCoverage[row + 1][i + 1], t);
    return lerp(a, b, rowT);
}

/// @brief 水面の 1 点の泡の見た目の割合
struct WaterFoamLayer
{
    float lace;    ///< 水面の上の白い泡（レース）[0,1]
    float haze;    ///< レースの穴の間と縁の外側の白濁 [0,1]
    float pattern; ///< 泡の内部パターン [0,1]
};

/// @brief 被覆率から、その点の泡のレースと白濁の割合を求める
/// @param mask      泡の被覆率 [0,1]（水面のうち泡のレースが占める面積の割合）
/// @param patternXZ 模様を評価する位置（泡の塊と一緒に運ばれる座標）
/// @param freshness 砕けた直後の厚い泡の割合 [0,1]（1 = 砕けた直後・0 = 時間が経った薄い泡）
/// @param filter    1 画素の大きさに合わせた模様のぼかし方
/// @details 古い泡ほどレースを薄く（水面が透けるように）し、白濁は砕けた直後の泡にだけ出す。
///          被覆率が kFoamPatternMinCoverage 以下の所と 1 画素が大きい所は、模様を使わず被覆率で塗る
WaterFoamLayer EvaluateFoamLayer(float mask, float2 patternXZ, float freshness, FoamPatternFilter filter)
{
    WaterFoamLayer layer;
    float lace = mask;
    layer.pattern = kFoamPatternMean;
    [branch]
    if (filter.flatten < 1.0f && mask > kFoamPatternMinCoverage)
    {
        layer.pattern = FoamPattern(patternXZ, gFoamPatternTime, filter.octaveFade);
        const float sharpLace = ComputeFoamLace(
            FoamLaceMaskForCoverage(mask, filter.tableLevel), layer.pattern, filter.edgeWidth);
        lace = lerp(sharpLace, mask, filter.flatten);
    }
    layer.lace = lace * lerp(kFoamAgedOpacity, 1.0f, freshness);
    layer.haze = saturate(mask * 1.2f) * freshness * (1.0f - lace)
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
/// @param grainFade 平均へ移す割合（FoamPatternFilter::grainFade）
float FoamGrain(float2 baseWorldXZ, float grainFade)
{
    const float grain = lerp(kFoamGrainMin, 1.0f, FoamValueNoise(baseWorldXZ * kFoamGrainScale));
    return lerp(grain, 0.5f * (kFoamGrainMin + 1.0f), grainFade);
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

    // 天空光（大気アクティブ時の Sky Irradiance SH）
    if (gSkyAmbientEnabled != 0)
    {
        lighting += EvaluateWaterSkyIrradiance(normal) * gSkyAmbientScale;
    }

    return kFoamAlbedo * lighting;
}

/// @brief 水面の 1 点の泡
struct WaterFoamResult
{
    float mask;        ///< 被覆率（白波の泡と岸の泡の大きい方）
    float surfaceFoam; ///< 水面の上の泡（レース）の割合。反射・グリッター・波頭の透過をこの割合だけ遮る
    float bubbleHaze;  ///< 水面下の気泡（白濁）の割合。透過してくる光にだけ混ざる
    float3 color;      ///< 泡の色
    float whitecapInstant; ///< 白波のうち、いま砕けている泡の被覆率
    float whitecapResidue; ///< 白波のうち、砕けた後に残っている泡の被覆率
    float shoreCoverage;   ///< 岸の泡の被覆率
};

/// @brief 白波の泡と岸の泡を重ね、水面の 1 点の泡の割合と色を求める
/// @param baseWorldXZ         変位前の参照格子座標
/// @param surfacePosition     水面の点（頂点変位後のワールド座標）
/// @param fallbackDepth       海底の高さの範囲の外で使う水深（視線の延長で測った鉛直水深）
/// @param normal              水面の法線
/// @param mainLightVisibility メインライト（0 番）の日向率（0=影 / 1=日向）
/// @details 白波の泡は FFT の参照格子座標で評価し、模様は泡と一緒に風下へ流す。
///          岸の泡は水の粒の静止位置で読み、模様もそこで評価する（寄せ・引きと一緒に動く）。
///          2 つの泡は模様の動きが違うので、別々にレース（lace）と白濁（haze）を作って重ねる。
///          白波の泡は、砕けた直後の厚い泡の割合で見た目を変える（岸の泡はいつも砕けた直後として扱う）。
WaterFoamResult ResolveWaterFoam(
    float2 baseWorldXZ, float3 surfacePosition, float fallbackDepth, float3 normal, float mainLightVisibility)
{
    // 画素ごとの差分を使うので、分岐より前で求める
    const FoamPatternFilter filter = ComputeFoamPatternFilter(baseWorldXZ);

    const WhitecapFoamCoverage whitecapCoverage = ComputeWhitecapFoamCoverage(baseWorldXZ);
    const float whitecapFoam = saturate(max(whitecapCoverage.instant, whitecapCoverage.residue));
    const float whitecapFreshness = saturate(
        max(whitecapCoverage.instant, whitecapCoverage.residueFresh) / max(whitecapFoam, 1.0e-4f));
    const ShoreFoamResult shoreFoam = ResolveShoreFoam(surfacePosition, fallbackDepth);
    const float2 whitecapPatternXZ = ComputeWhitecapPatternXZ(baseWorldXZ);
    const float2 shorePatternXZ = baseWorldXZ + shoreFoam.patternShift;
    WaterFoamLayer whitecapLayer = (WaterFoamLayer)0;
    [branch]
    if (whitecapFoam > 0.0f)
    {
        whitecapLayer = EvaluateFoamLayer(whitecapFoam, whitecapPatternXZ, whitecapFreshness, filter);
    }
    WaterFoamLayer shoreLayer = (WaterFoamLayer)0;
    [branch]
    if (shoreFoam.coverage > 0.0f)
    {
        shoreLayer = EvaluateFoamLayer(shoreFoam.coverage, shorePatternXZ, 1.0f, filter);
    }
    const WaterFoamLayer foamLayer = CombineFoamLayers(whitecapLayer, shoreLayer);

    WaterFoamResult result;
    result.mask = max(whitecapFoam, shoreFoam.coverage);
    result.whitecapInstant = whitecapCoverage.instant;
    result.whitecapResidue = whitecapCoverage.residue;
    result.shoreCoverage = shoreFoam.coverage;
    result.surfaceFoam = foamLayer.lace * saturate(gFoamOpacity);
    result.bubbleHaze = foamLayer.haze * saturate(gFoamOpacity) * kFoamHazeOpacity;

    // 泡の色（Lambert 白 × 太陽直達 + 天空光）。気泡の粒感として明度を高周波ノイズで揺らす
    result.color = float3(0.0f, 0.0f, 0.0f);
    if (result.mask > 0.0f)
    {
        const float shoreFoamShare = shoreFoam.coverage / max(whitecapFoam + shoreFoam.coverage, 1.0e-4f);
        const float foamGrain = lerp(
            FoamGrain(whitecapPatternXZ, filter.grainFade), FoamGrain(shorePatternXZ, filter.grainFade), shoreFoamShare);
        result.color = ComputeFoamColor(normal, mainLightVisibility) * foamGrain;
    }
    return result;
}

#endif // WATER_FOAM_APPEARANCE_INCLUDED

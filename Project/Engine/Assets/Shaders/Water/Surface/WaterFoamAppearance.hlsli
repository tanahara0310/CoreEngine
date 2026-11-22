// ============================================================
// 泡の見た目（模様・形・色）（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 被覆率（WaterFoamCoverage.hlsli）を、レース状の泡の形・水の中の気泡の濁り・泡の色へ変える。
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
// ---- 水の中の気泡（気泡の量 B から描く青白い濁り）----
// 網目の穴をあけず、ゆるいまだらだけを掛けて、透過してくる光に √B の割合で混ぜる。
// 色は泡の色に、気泡の雲の中で散乱しながら進む道のりの分の水の吸収を掛けたもの
static const float kFoamBubbleOpacity = 0.5f;          // 気泡の量 1 のときに透過の光へ混ぜる割合
static const float kFoamBubbleBrightness = 0.6f;       // 泡の色に対する明るさの比
static const float kFoamBubblePathMeters = 1.2f;       // 吸収を掛ける道のり [m]
static const float kFoamBubbleMottleFrequencies[2] = { 2.0f, 4.7f }; // まだらの細かさ [1/m]
static const float kFoamBubbleMottleWeights[2] = { 0.65f, 0.35f };
static const float kFoamBubbleMottleMin = 0.45f;       // まだらの薄い所の割合
// 砕けてから時間が経った薄い泡の、レースの不透明度の割合（砕けた直後を 1 とする。厚みを使わないとき）
static const float kFoamAgedOpacity = 0.6f;

// ---- 泡の見た目の当て方の項目（gFoamLookFlags のビット。C++ の WaterFoamLook と同じ値）----
static const uint kFoamLookOpacity = 1u; // 泡の厚みで不透明度を決める（新しく厚い泡は不透明）
static const uint kFoamLookEdge = 2u;    // 泡の厚みで縁の柔らかさを変える（古い泡ほど柔らかい）
static const uint kFoamLookLight = 4u;   // 泡と水の中の気泡に空の光をそのまま当て、太陽の光を回り込ませる
// ---- 泡の厚み（新しさと被覆率から作る [0,1]。1 = 砕けたばかりの厚い泡・0 = 時間が経った 1 層の薄い泡）----
static const float kFoamThinOpacity = 0.55f;   // 厚み 0 の泡のレースの不透明度
static const float kFoamSoftEdgeWidth = 0.22f; // 厚み 0 の泡の dissolve のしきい値の前後の幅
static const float kFoamLightWrap = 0.5f;      // 太陽の光を N·L = -kFoamLightWrap まで回り込ませる
// 泡内部の粒状の明度変調（気泡の粒感。周期 ≈ 8cm）
static const float kFoamGrainScale = 13.0f;
static const float kFoamGrainMin = 0.82f;
// 泡域でのグリッター用ラフネス（泡は微細気泡でハイライトが大きく柔らかくなる）
static const float kFoamGlintRoughness = 0.45f;
// 物がかき回した所の泡の模様を、模様を動かす速さ v で v × (位相 × 周期) だけずらす（フローマップ。周期は kObjectFoamFlowPeriodSeconds）。
// 半周期ずらした 2 つの位相の模様を三角の重みで重ね、ずれが 0 へ戻る瞬間の模様の重みを 0 にする。
// 位相は場所ごとにゆっくり変わるノイズでずらす
static const float kObjectFoamFlowPhaseFrequency = 0.35f; // 位相をずらすノイズの細かさ [1/m]

// ---- 泡の模様（泡の膜が割れて残る網目。gFoamPatternMode が 0 のとき）----
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
// 段を平均へ移し始める・移し終える 1 画素の幅（マスの大きさに対する割合。粒の明度変調も粒の周期に対して同じ割合で移す）
static const float kFoamEdgeFadeStart = 0.15f;
static const float kFoamEdgeFadeEnd = 0.40f;
// dissolve のしきい値の前後の幅の上限
static const float kFoamMaxEdgeWidth = 0.1f;
// 模様をやめて被覆率の平均へ移し始める・移し終える 1 画素の幅 [m]
static const float kFoamFlattenStart = 0.6f;
static const float kFoamFlattenEnd = 1.6f;
// これより薄い泡は模様を使わず被覆率で塗る
static const float kFoamPatternMinCoverage = 0.004f;
// 泡の明るさの変調の平均（遠くでその値へ移す）
static const float kFoamGrainMean = 0.5f * (kFoamGrainMin + 1.0f);

// ---- 泡の模様の画像（gFoamPatternMode が 1 のとき）----
// 網目の画像を 2 つの大きさ（4m と 1.3m。1.3m は向きを変え、時間でずらす）で読んで重みを付けて足し、
// 近くでは気泡の粒の画像も足す。どの画像の R も [0,1] に一様なので、足した値を台形の分布の式で一様へ戻す。
// 一様なので、被覆率 c の泡は「値が 1 - c より大きい所」になる。
// Tools/Water/generate_foam_textures.py が画像と、ミップの散らばりの表・塊の場を掛けたときの表を作る
static const float kFoamNetTileMeters = 4.0f;    // 網目の画像 1 枚の範囲 [m]
static const float kFoamNetTexels = 1024.0f;     // 網目の画像の一辺の画素数
static const float kFoamDetailTileMeters = 1.3f; // 2 つ目の大きさで読む網目の画像 1 枚の範囲 [m]
static const float kFoamDetailAngle = 0.9f;      // 2 つ目の網目の回転 [rad]
static const float kFoamDetailWeight = 0.4f;     // 2 つ目の網目の重み
// 模様を動かす時刻の 1 周期（kFoamPatternPeriodSeconds）に 2 つ目の網目をずらす量（画像の枚数。整数にして周期の継ぎ目でつなぐ）
static const float2 kFoamDetailSlideTiles = float2(7.0f, 4.0f);
static const float kFoamDetailFadeStart = 0.12f; // 2 つ目の網目をやめ始める・やめ終える 1 画素の幅 [m]
static const float kFoamDetailFadeEnd = 0.25f;
static const float kFoamGrainTileMeters = 0.5f;  // 気泡の粒の画像 1 枚の範囲 [m]
static const float kFoamGrainTexels = 512.0f;    // 気泡の粒の画像の一辺の画素数
static const float kFoamGrainWeight = 0.25f;     // 気泡の粒の重み
static const float kFoamGrainLodFadeStart = 2.0f; // 気泡の粒をやめ始める・やめ終えるミップ
static const float kFoamGrainLodFadeEnd = 4.0f;
static const float kFoamTextureFlattenStart = 0.4f; // 模様をやめて被覆率の平均へ移し始める・移し終える 1 画素の幅 [m]
static const float kFoamTextureFlattenEnd = 1.2f;
/// @brief 泡の模様の画像のミップごとの、双一次で読んだ R の分散と次のミップとの共分散（一様な値の分散 1/12 を 1 とする）
/// @details [0] = 網目・[1] = 気泡の粒。足りない段は最後の段と同じ
static const float2 kFoamTextureMipSpread[2][11] = {
    {
        float2(0.9596f, 0.8857f), float2(0.9858f, 0.9227f), float2(0.9831f, 0.9071f), float2(0.9795f, 0.8776f),
        float2(0.9705f, 0.8357f), float2(0.9719f, 0.8155f), float2(0.9535f, 0.7690f), float2(0.8748f, 0.6760f),
        float2(0.7459f, 0.4525f), float2(0.3316f, 0.0000f), float2(0.0000f, 0.0000f),
    },
    {
        float2(0.9366f, 0.8366f), float2(0.9839f, 0.9058f), float2(0.9803f, 0.8911f), float2(0.9737f, 0.8583f),
        float2(0.9613f, 0.7884f), float2(0.9252f, 0.6433f), float2(0.9217f, 0.7862f), float2(0.7532f, 0.3945f),
        float2(0.3324f, 0.0000f), float2(0.0000f, 0.0000f), float2(0.0000f, 0.0000f),
    },
};
/// @brief 泡の模様の画像のミップごとの、隣の画素との R の差の平均
static const float kFoamTextureMipChange[2][11] = {
    { 0.065f, 0.047f, 0.054f, 0.064f, 0.074f, 0.084f, 0.102f, 0.148f, 0.251f, 0.375f, 0.000f },
    { 0.089f, 0.054f, 0.060f, 0.070f, 0.089f, 0.119f, 0.127f, 0.234f, 0.375f, 0.000f, 0.000f },
};
/// @brief 塊の場を掛けた泡の画像の模様で、被覆率 c に対する dissolve のマスク（列は √c の等間隔・c = (k / 32)²）
static const float kFoamTextureClusterMask[33] = {
    0.0000f, 0.0145f, 0.0303f, 0.0486f, 0.0738f, 0.1170f, 0.1573f, 0.1947f,
    0.2299f, 0.2633f, 0.2954f, 0.3264f, 0.3563f, 0.3853f, 0.4139f, 0.4419f,
    0.4695f, 0.4968f, 0.5237f, 0.5503f, 0.5769f, 0.6036f, 0.6303f, 0.6571f,
    0.6840f, 0.7113f, 0.7390f, 0.7672f, 0.7967f, 0.8286f, 0.8645f, 0.9112f,
    1.1210f,
};

// 段ごとの値の平均（遠くでその段を平均へ移すときに使う）
static const float kFoamEdgeMeans[3] = { 0.1870f, 0.2643f, 0.3673f };
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

/// @brief 3 段の網目の線を重みで足した値
/// @param xz         模様を評価する位置 [m]
/// @param time       模様を動かす時刻 [s]
/// @param octaveFade 段ごとに平均へ移す割合（1 の段は評価しない）
float FoamNet(float2 xz, float time, float3 octaveFade)
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
    return net;
}

/// @brief 網目の値と塊の場から、泡の模様の値を求める [0,1]
float CombineFoamPattern(float net, float cluster)
{
    return saturate(cluster * (0.3f + 0.7f * net) + 0.2f * net);
}

/// @brief 泡の模様 [0,1]（dissolve のしきい値の場。値の大きい所から泡になる）
/// @param xz         模様を評価する位置 [m]
/// @param time       模様を動かす時刻 [s]
/// @param octaveFade 段ごとに平均へ移す割合（1 の段は評価しない）
float FoamPattern(float2 xz, float time, float3 octaveFade)
{
    return CombineFoamPattern(FoamNet(xz, time, octaveFade), FoamClusterField(xz));
}

/// @brief 1 画素の大きさに合わせた泡の模様のぼかし方
struct FoamPatternFilter
{
    float3 octaveFade; ///< 段ごとに平均へ移す割合（細かい段から 1 になる）
    float tableLevel;  ///< 面積の表の段（平均へ移した段の数 [0,3]）
    float edgeWidth;   ///< dissolve のしきい値の前後の幅
    float flatten;     ///< 模様をやめて被覆率の平均へ移す割合
    float grainFade;   ///< 粒の明度変調を平均へ移す割合
    float2 mottleFade; ///< 気泡のまだらの段ごとに平均へ移す割合
    float footprint;   ///< 1 画素の幅 [m]
    float textureFlatten; ///< 泡の模様の画像をやめて被覆率の平均へ移す割合
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
    filter.grainFade = saturate(
        (footprint * kFoamGrainScale - kFoamEdgeFadeStart) / (kFoamEdgeFadeEnd - kFoamEdgeFadeStart));
    [unroll]
    for (uint mottle = 0; mottle < 2; ++mottle)
    {
        filter.mottleFade[mottle] = saturate(
            (footprint * kFoamBubbleMottleFrequencies[mottle] - kFoamEdgeFadeStart) / (kFoamEdgeFadeEnd - kFoamEdgeFadeStart));
    }
    filter.footprint = footprint;
    filter.textureFlatten = saturate(
        (footprint - kFoamTextureFlattenStart) / (kFoamTextureFlattenEnd - kFoamTextureFlattenStart));
    return filter;
}

/// @brief 重み a・b（a + b = 1）で足した 2 つの一様な値 [0,1] の和を、[0,1] の一様な値へ戻す
/// @details 和の分布は台形なので、その累積分布を返す
float EqualizeUniformSum(float x, float a, float b)
{
    const float lo = min(a, b);
    const float hi = max(a, b);
    if (lo < 1.0e-3f)
    {
        return x;
    }
    if (x < lo)
    {
        return x * x / (2.0f * a * b);
    }
    if (x > hi)
    {
        const float y = 1.0f - x;
        return 1.0f - y * y / (2.0f * a * b);
    }
    return (x - 0.5f * lo) / hi;
}

/// @brief 1 画素の幅から、泡の模様の画像のミップを求める [0,10]
/// @param texelMeters ミップ 0 の 1 画素の大きさ [m]
float FoamTextureLod(float footprint, float texelMeters)
{
    return clamp(log2(max(footprint, 1.0e-6f) / texelMeters), 0.0f, 10.0f);
}

/// @brief 三線形で読んだ泡の模様の画像の R を、[0,1] の一様な値へ戻す
/// @param textureIndex 0 = 網目・1 = 気泡の粒
/// @details 双一次の補間とミップの混ぜ合わせで値が 0.5 へ寄った分を、同じ分散の台形の分布
///          （重み 1 - b と b の 2 つの一様な値の和）とみなして戻す
float EqualizeFoamMipRead(float value, float lod, uint textureIndex)
{
    const uint level = min((uint)lod, 9u);
    const float f = saturate(lod - (float)level);
    const float2 spread = kFoamTextureMipSpread[textureIndex][level];
    const float nextVariance = kFoamTextureMipSpread[textureIndex][level + 1u].x;
    const float variance = (1.0f - f) * (1.0f - f) * spread.x + f * f * nextVariance
        + 2.0f * f * (1.0f - f) * spread.y;
    const float b = 0.5f * (1.0f - sqrt(max(2.0f * variance - 1.0f, 0.0f)));
    return EqualizeUniformSum(value, 1.0f - b, b);
}

/// @brief 泡の模様の画像の 1 画素の間の R の変化の大きさ（dissolve の幅の目安）
/// @param texelMeters ミップ 0 の 1 画素の大きさ [m]
/// @param textureIndex 0 = 網目・1 = 気泡の粒
float FoamTextureChange(float footprint, float texelMeters, float lod, uint textureIndex)
{
    const uint level = min((uint)lod, 9u);
    const float f = saturate(lod - (float)level);
    const float change = lerp(
        kFoamTextureMipChange[textureIndex][level], kFoamTextureMipChange[textureIndex][level + 1u], f);
    return change * min(footprint / texelMeters, 1.0f);
}

/// @brief 泡の模様の画像から読んだ値
struct FoamTextureSample
{
    float order;      ///< 泡になる順 [0,1]（一様に散らばる。大きい所から泡になる）
    float brightness; ///< 泡の明るさ [0,1]
    float change;     ///< 1 画素の間の order の変化の大きさ
};

/// @brief 泡の模様の画像の値に、もう 1 つの一様な値を重み weight で混ぜて一様へ戻す
void BlendFoamTextureSample(inout FoamTextureSample foamSample, float order, float brightness, float change, float weight)
{
    const float keep = 1.0f - weight;
    foamSample.order = EqualizeUniformSum(keep * foamSample.order + weight * order, keep, weight);
    foamSample.brightness = lerp(foamSample.brightness, brightness, weight);
    foamSample.change = (keep * foamSample.change + weight * change) / max(keep, weight);
}

/// @brief 泡の模様の画像を読む
/// @param xz        模様を評価する位置 [m]
/// @param footprint 1 画素の幅 [m]
/// @details 網目を 4m と 1.3m（向きを変え、模様を動かす時刻でずらす）で読んで混ぜ、近くでは気泡の粒も混ぜる。
///          2 つ目の網目は 1 画素が kFoamDetailFadeStart〜End の間で、気泡の粒はミップ
///          kFoamGrainLodFadeStart〜End の間で重みを 0 へ下げる
FoamTextureSample SampleFoamTexture(float2 xz, float footprint)
{
    const float netTexel = kFoamNetTileMeters / kFoamNetTexels;
    const float netLod = FoamTextureLod(footprint, netTexel);
    const float2 net = gWaterFoamNet.SampleLevel(gSampler, xz / kFoamNetTileMeters, netLod);
    FoamTextureSample foamSample;
    foamSample.order = EqualizeFoamMipRead(net.x, netLod, 0u);
    foamSample.brightness = net.y;
    foamSample.change = FoamTextureChange(footprint, netTexel, netLod, 0u);

    const float detailWeight = kFoamDetailWeight
        * (1.0f - saturate((footprint - kFoamDetailFadeStart) / (kFoamDetailFadeEnd - kFoamDetailFadeStart)));
    [branch]
    if (detailWeight > 0.0f)
    {
        float s;
        float c;
        sincos(kFoamDetailAngle, s, c);
        const float2 rotated = float2(c * xz.x - s * xz.y, s * xz.x + c * xz.y);
        const float2 uv = rotated / kFoamDetailTileMeters
            + kFoamDetailSlideTiles * (gFoamPatternTime / kFoamPatternPeriodSeconds);
        const float detailTexel = kFoamDetailTileMeters / kFoamNetTexels;
        const float detailLod = FoamTextureLod(footprint, detailTexel);
        const float2 detail = gWaterFoamNet.SampleLevel(gSampler, uv, detailLod);
        BlendFoamTextureSample(foamSample, EqualizeFoamMipRead(detail.x, detailLod, 0u), detail.y,
            FoamTextureChange(footprint, detailTexel, detailLod, 0u), detailWeight);
    }

    const float grainTexel = kFoamGrainTileMeters / kFoamGrainTexels;
    const float grainLod = FoamTextureLod(footprint, grainTexel);
    const float grainWeight = kFoamGrainWeight
        * (1.0f - saturate((grainLod - kFoamGrainLodFadeStart) / (kFoamGrainLodFadeEnd - kFoamGrainLodFadeStart)));
    [branch]
    if (grainWeight > 0.0f)
    {
        const float2 grain = gWaterFoamGrain.SampleLevel(gSampler, xz / kFoamGrainTileMeters, grainLod);
        BlendFoamTextureSample(foamSample, EqualizeFoamMipRead(grain.x, grainLod, 1u), grain.y,
            FoamTextureChange(footprint, grainTexel, grainLod, 1u), grainWeight);
    }
    return foamSample;
}

/// @brief 塊の場を掛けた泡の模様の画像で、被覆率 [0,1] をレースの面積がその割合になる dissolve のマスクへ変える
float FoamTextureClusterMask(float coverage)
{
    const float x = sqrt(saturate(coverage)) * 32.0f;
    const uint i = min((uint)x, 31u);
    return lerp(kFoamTextureClusterMask[i], kFoamTextureClusterMask[i + 1u], x - (float)i);
}

/// @brief 泡の見た目の当て方の項目が有効か
/// @param item kFoamLook* のどれか
bool FoamLookEnabled(uint item)
{
    return ((uint)gFoamLookFlags & item) != 0u;
}

/// @brief 泡の厚み [0,1]（1 = 砕けたばかりの厚い泡・0 = 時間が経った 1 層の薄い泡）
/// @param mask      泡の被覆率 [0,1]
/// @param freshness 砕けた直後の厚い泡の割合 [0,1]
/// @details 新しさの平方根と被覆率の大きい方（泡が多く集まった所は時間が経っても厚い）
float FoamThickness(float mask, float freshness)
{
    return saturate(max(sqrt(saturate(freshness)), mask));
}

/// @brief 被覆率マスク → レース状の泡形状 [0,1]（dissolve カット）
/// @details 模様がしきい値 (1-mask) を超えた場所が泡になる。しきい値の前後 edgeWidth で 0 から 1 へ上がる。
///          被覆率が上がると模様の高い所から順に泡が「埋まって」いく。
float ComputeFoamLace(float mask, float pattern, float edgeWidth)
{
    const float center = 1.0f - mask + 0.5f * kFoamLaceSoftness;
    return smoothstep(center - edgeWidth, center + edgeWidth, pattern);
}

/// @brief 泡の厚みに合わせた dissolve のしきい値の前後の幅
/// @param mask      ComputeFoamLace に渡すマスク
/// @param edgeWidth 1 画素の模様の変化から決めた幅（厚み 1 の泡の幅）
/// @param thickness 泡の厚み [0,1]（FoamThickness）
/// @details 薄い泡ほど幅を kFoamSoftEdgeWidth へ広げる。しきい値の前後が [0,1] からはみ出さない幅までにして、
///          レースの面積を被覆率のまま保つ
float FoamEdgeWidthForThickness(float mask, float edgeWidth, float thickness)
{
    const float center = 1.0f - mask + 0.5f * kFoamLaceSoftness;
    const float room = min(center, 1.0f - center);
    return max(min(lerp(kFoamSoftEdgeWidth, edgeWidth, thickness), room), edgeWidth);
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

/// @brief 泡の内部の粒状の明度変調 [kFoamGrainMin, 1]
/// @param grainFade 平均へ移す割合（FoamPatternFilter::grainFade）
float FoamGrain(float2 baseWorldXZ, float grainFade)
{
    const float grain = lerp(kFoamGrainMin, 1.0f, FoamValueNoise(baseWorldXZ * kFoamGrainScale));
    return lerp(grain, 0.5f * (kFoamGrainMin + 1.0f), grainFade);
}

/// @brief 泡の模様の画像で、被覆率からレース状の泡の形を求める [0,1]
/// @param thickness  泡の厚み [0,1]（FoamThickness。kFoamLookEdge のとき縁の柔らかさに使う）
/// @param brightness 泡の明るさの変調 [kFoamGrainMin, 1]
/// @details 画像の R は一様なので、塊の場を掛けないときは被覆率をそのまましきい値に使う。
///          塊の場を掛けるときは、掛けた後の分布の表（kFoamTextureClusterMask）でしきい値へ直す
float EvaluateFoamTextureLace(
    float mask, float2 patternXZ, FoamPatternFilter filter, bool clustered, float thickness, out float brightness)
{
    float lace = mask;
    brightness = kFoamGrainMean;
    [branch]
    if (filter.textureFlatten < 1.0f && mask > kFoamPatternMinCoverage)
    {
        const FoamTextureSample foamSample = SampleFoamTexture(patternXZ, filter.footprint);
        float pattern = foamSample.order;
        float laceMask = mask + 0.5f * kFoamLaceSoftness;
        float slope = 1.0f;
        if (clustered)
        {
            const float cluster = FoamClusterField(patternXZ);
            pattern = CombineFoamPattern(foamSample.order, cluster);
            laceMask = FoamTextureClusterMask(mask);
            slope = 0.7f * cluster + 0.2f;
        }
        float edgeWidth = clamp(0.5f * slope * foamSample.change, 0.5f * kFoamLaceSoftness, kFoamMaxEdgeWidth);
        if (FoamLookEnabled(kFoamLookEdge))
        {
            edgeWidth = FoamEdgeWidthForThickness(laceMask, edgeWidth, thickness);
        }
        lace = lerp(ComputeFoamLace(laceMask, pattern, edgeWidth), mask, filter.textureFlatten);
        brightness = lerp(lerp(kFoamGrainMin, 1.0f, foamSample.brightness), kFoamGrainMean, filter.textureFlatten);
    }
    return lace;
}

/// @brief 被覆率から、その点の水面の上の白い泡（レース）の割合を求める [0,1]
/// @param mask      泡の被覆率 [0,1]（水面のうち泡のレースが占める面積の割合）
/// @param patternXZ 模様を評価する位置（泡の塊と一緒に運ばれる座標）
/// @param freshness 砕けた直後の厚い泡の割合 [0,1]（1 = 砕けた直後・0 = 時間が経った薄い泡）
/// @param filter    1 画素の大きさに合わせた模様のぼかし方
/// @param clustered true = 塊の場で濃く出る所と抜ける所を分け、面積の表で被覆率をしきい値へ直す。
///                  false = 塊の場を掛けず（どこも塊の場 1 として）被覆率をそのまま網目のしきい値に使う
/// @param brightness 泡の明るさの変調 [kFoamGrainMin, 1]
/// @details 模様は gFoamPatternMode で選ぶ（0 = ボロノイの網目・1 = 泡の模様の画像）。
///          古い泡ほどレースを薄く（水面が透けるように）する。kFoamLookOpacity のときは泡の厚みで
///          厚み 1 を不透明・厚み 0 を kFoamThinOpacity にし、kFoamLookEdge のときは薄い泡ほど縁を柔らかくする。
///          被覆率が kFoamPatternMinCoverage 以下の所と 1 画素が大きい所は、模様を使わず被覆率で塗る
float EvaluateFoamLace(
    float mask, float2 patternXZ, float freshness, FoamPatternFilter filter, bool clustered, out float brightness)
{
    const float thickness = FoamThickness(mask, freshness);
    float lace = mask;
    [branch]
    if (gFoamPatternMode != 0)
    {
        lace = EvaluateFoamTextureLace(mask, patternXZ, filter, clustered, thickness, brightness);
    }
    else
    {
        brightness = FoamGrain(patternXZ, filter.grainFade);
        [branch]
        if (filter.flatten < 1.0f && mask > kFoamPatternMinCoverage)
        {
            float laceMask = mask;
            float pattern;
            if (clustered)
            {
                pattern = FoamPattern(patternXZ, gFoamPatternTime, filter.octaveFade);
                laceMask = FoamLaceMaskForCoverage(mask, filter.tableLevel);
            }
            else
            {
                pattern = CombineFoamPattern(FoamNet(patternXZ, gFoamPatternTime, filter.octaveFade), 1.0f);
            }
            const float edgeWidth = FoamLookEnabled(kFoamLookEdge)
                ? FoamEdgeWidthForThickness(laceMask, filter.edgeWidth, thickness)
                : filter.edgeWidth;
            const float sharpLace = ComputeFoamLace(laceMask, pattern, edgeWidth);
            lace = lerp(sharpLace, mask, filter.flatten);
        }
    }
    const float opacity = FoamLookEnabled(kFoamLookOpacity)
        ? lerp(kFoamThinOpacity, 1.0f, thickness)
        : lerp(kFoamAgedOpacity, 1.0f, freshness);
    return lace * opacity;
}

/// @brief 水の中の気泡のゆるいまだら [kFoamBubbleMottleMin, 1]
/// @param patternXZ 気泡と一緒に運ばれる座標 [m]
float FoamBubbleMottle(float2 patternXZ, FoamPatternFilter filter)
{
    float mottle = 0.0f;
    [unroll]
    for (uint octave = 0; octave < 2; ++octave)
    {
        const float noise = FoamValueNoise(
            patternXZ * kFoamBubbleMottleFrequencies[octave] + float2(23.9f, -7.1f) * (float)(octave + 1u));
        mottle += kFoamBubbleMottleWeights[octave] * lerp(noise, 0.5f, filter.mottleFade[octave]);
    }
    return lerp(kFoamBubbleMottleMin, 1.0f, mottle);
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

/// @brief 物がかき回した所の泡の模様を評価する 2 つの位置と重み
struct ObjectFoamPattern
{
    float2 patternXZ[2]; ///< 位相ごとの模様を評価する位置
    float weight[2];     ///< 位相ごとの重み（和は 1）
};

/// @brief 物がかき回した所の泡の模様を評価する位置（泡と一緒に風下へ流し、模様を動かす速さでずらし、風の向きに伸ばす）
/// @param baseWorldXZ     変位前の参照格子座標（FFT でない水は世界の XZ）
/// @param patternVelocity 模様を動かす速さ [m/s]（ObjectFoamSample::patternVelocity）
ObjectFoamPattern ComputeObjectFoamPattern(float2 baseWorldXZ, float2 patternVelocity)
{
    const float2 driftedXZ = baseWorldXZ - gObjectFoamDriftOffsetXZ;
    const float phaseOffset = FoamValueNoise(baseWorldXZ * kObjectFoamFlowPhaseFrequency);
    const float basePhase = frac(gObjectFoamFlowTime / kObjectFoamFlowPeriodSeconds + phaseOffset);
    ObjectFoamPattern pattern;
    [unroll]
    for (uint i = 0; i < 2; ++i)
    {
        const float phase = frac(basePhase + 0.5f * (float)i);
        const float2 shiftedXZ = driftedXZ - patternVelocity * (phase * kObjectFoamFlowPeriodSeconds);
        pattern.patternXZ[i] = shiftedXZ - gFoamStretchAxis * dot(gFoamStretchAxis, shiftedXZ);
        pattern.weight[i] = 1.0f - abs(2.0f * phase - 1.0f);
    }
    return pattern;
}

/// @brief 2 つのレースを重ねる（和集合）
float CombineFoamLace(float a, float b)
{
    return 1.0f - (1.0f - a) * (1.0f - b);
}

/// @brief 泡レイヤの表面色（Lambert 白 × 太陽直達 + 天空光）
/// @details kFoamLookLight のときは、天空光を水の中の光と同じく Sky Irradiance SH のまま当て、
///          太陽の光は泡の中で散乱して回り込む分として N·L = -kFoamLightWrap まで当てる。
///          そうでないときは、天空光を地面の環境光と同じく gSkyAmbientScale 倍し、太陽の光は N·L で当てる。
///          太陽ライトの色には大気の Transmittance 減衰が乗算済みなので、
///          日没時は泡も自動的に赤みを帯びて暗くなる。
/// @param mainLightVisibility メインライト（0 番）の日向率（0=影 / 1=日向）
float3 ComputeFoamColor(float3 normal, float mainLightVisibility)
{
    const bool scatteredLight = FoamLookEnabled(kFoamLookLight);

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
        const float cosine = dot(normal, lightVec);
        const float diffuse = scatteredLight
            ? saturate((cosine + kFoamLightWrap) / (1.0f + kFoamLightWrap))
            : saturate(cosine);
        lighting += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity
            * diffuse * visibility / PI;
    }

    // 天空光（大気アクティブ時の Sky Irradiance SH）
    if (gSkyAmbientEnabled != 0)
    {
        lighting += EvaluateWaterSkyIrradiance(normal) * (scatteredLight ? 1.0f : gSkyAmbientScale);
    }

    return kFoamAlbedo * lighting;
}

/// @brief 水面の 1 点の泡
struct WaterFoamResult
{
    float mask;        ///< 被覆率（白波の泡・物がかき回した所の泡・岸の泡のいちばん大きいもの）
    float surfaceFoam; ///< 水面の上の泡（レース）の割合。反射・グリッター・波頭の透過をこの割合だけ遮る
    float bubbles;     ///< 水の中の気泡の量（白波・物がかき回した所・岸のいちばん大きいもの）[0,1]
    float bubbleHaze;  ///< 水の中の気泡の濁りの割合。透過してくる光にだけ混ざる
    float3 color;      ///< 泡の色
    float3 bubbleColor; ///< 水の中の気泡の色
    float whitecapInstant; ///< 白波のうち、いま砕けている泡の被覆率
    float whitecapResidue; ///< 白波のうち、砕けた後に残っている泡の被覆率
    float wakeCoverage;    ///< 物がかき回した所に残っている泡の被覆率
    float shoreCoverage;   ///< 岸の泡の被覆率
};

/// @brief 白波の泡・物がかき回した所の泡・岸の泡を重ね、水面の 1 点の泡の割合と色を求める
/// @param baseWorldXZ         変位前の参照格子座標
/// @param surfacePosition     水面の点（頂点変位後のワールド座標）
/// @param fallbackDepth       海底の高さの範囲の外で使う水深（視線の延長で測った鉛直水深）
/// @param normal              水面の法線
/// @param mainLightVisibility メインライト（0 番）の日向率（0=影 / 1=日向）
/// @details 白波の泡は FFT の参照格子座標で評価し、模様は泡と一緒に風下へ流す。
///          物がかき回した所の泡は物の泡の格子から同じ座標で読み、模様は泡と一緒に風下へ流し、
///          模様を動かす速さでずらした 2 つの位相の模様を重ねる（ComputeObjectFoamPattern）。
///          塊の場を掛けない模様で、細い帯でも途切れずに描く。
///          岸の泡は水の粒の静止位置で読み、模様もそこで評価する（寄せ・引きと一緒に動く）。
///          それぞれ別々にレース（lace）を作って重ねる。
///          白波と物がかき回した所の泡は、気泡が多い（新しい）ほどレースを不透明にする（岸の泡はいつも新しい泡として扱う）。
///          水の中の気泡は、白波（いま砕けている泡と残りの気泡）・物がかき回した所の気泡・岸の泡の量から、
///          網目の穴をあけずにゆるいまだらだけを掛けて描く
WaterFoamResult ResolveWaterFoam(
    float2 baseWorldXZ, float3 surfacePosition, float fallbackDepth, float3 normal, float mainLightVisibility)
{
    // 画素ごとの差分を使うので、分岐より前で求める
    const FoamPatternFilter filter = ComputeFoamPatternFilter(baseWorldXZ);

    const WhitecapFoamCoverage whitecapCoverage = ComputeWhitecapFoamCoverage(baseWorldXZ);
    const float whitecapFoam = saturate(max(whitecapCoverage.instant, whitecapCoverage.residue));
    const float whitecapBubbles = saturate(max(whitecapCoverage.instant, whitecapCoverage.residueBubbles));
    const float whitecapFreshness = saturate(whitecapBubbles / max(whitecapFoam, 1.0e-4f));
    const ObjectFoamSample objectFoam = SampleObjectFoam(baseWorldXZ);
    const float wakeFoam = objectFoam.foam;
    const float wakeBubbles = objectFoam.bubbles;
    const float wakeFreshness = saturate(wakeBubbles / max(wakeFoam, 1.0e-4f));
    const ShoreFoamResult shoreFoam = ResolveShoreFoam(surfacePosition, fallbackDepth);
    const float2 whitecapPatternXZ = ComputeWhitecapPatternXZ(baseWorldXZ);
    const ObjectFoamPattern wakePattern = ComputeObjectFoamPattern(baseWorldXZ, objectFoam.patternVelocity);
    const float2 shorePatternXZ = baseWorldXZ + shoreFoam.patternShift;
    float whitecapLace = 0.0f;
    float whitecapGrain = kFoamGrainMean;
    [branch]
    if (whitecapFoam > 0.0f)
    {
        whitecapLace = EvaluateFoamLace(whitecapFoam, whitecapPatternXZ, whitecapFreshness, filter, true, whitecapGrain);
    }
    float wakeLace = 0.0f;
    float wakeGrain = kFoamGrainMean;
    [branch]
    if (wakeFoam > 0.0f)
    {
        wakeGrain = 0.0f;
        [unroll]
        for (uint phase = 0; phase < 2; ++phase)
        {
            float phaseGrain;
            wakeLace += wakePattern.weight[phase]
                * EvaluateFoamLace(wakeFoam, wakePattern.patternXZ[phase], wakeFreshness, filter, false, phaseGrain);
            wakeGrain += wakePattern.weight[phase] * phaseGrain;
        }
    }
    float shoreLace = 0.0f;
    float shoreGrain = kFoamGrainMean;
    [branch]
    if (shoreFoam.coverage > 0.0f)
    {
        shoreLace = EvaluateFoamLace(shoreFoam.coverage, shorePatternXZ, 1.0f, filter, true, shoreGrain);
    }
    const float lace = CombineFoamLace(CombineFoamLace(whitecapLace, wakeLace), shoreLace);

    // 水の中の気泡。白波と物がかき回した所の気泡はそれぞれの泡の模様の座標、岸の気泡は水の粒の静止位置でまだらを付ける
    const float openBubbles = max(whitecapBubbles, wakeBubbles);
    float bubbleHaze = 0.0f;
    [branch]
    if (whitecapBubbles > 0.0f)
    {
        bubbleHaze = sqrt(whitecapBubbles) * FoamBubbleMottle(whitecapPatternXZ, filter);
    }
    [branch]
    if (wakeBubbles > 0.0f)
    {
        const float wakeMottle = wakePattern.weight[0] * FoamBubbleMottle(wakePattern.patternXZ[0], filter)
            + wakePattern.weight[1] * FoamBubbleMottle(wakePattern.patternXZ[1], filter);
        bubbleHaze = max(bubbleHaze, sqrt(wakeBubbles) * wakeMottle);
    }
    [branch]
    if (shoreFoam.coverage > 0.0f)
    {
        bubbleHaze = max(bubbleHaze, sqrt(shoreFoam.coverage) * FoamBubbleMottle(shorePatternXZ, filter));
    }

    WaterFoamResult result;
    const float openFoam = max(whitecapFoam, wakeFoam);
    result.mask = max(openFoam, shoreFoam.coverage);
    result.bubbles = max(openBubbles, shoreFoam.coverage);
    result.whitecapInstant = whitecapCoverage.instant;
    result.whitecapResidue = whitecapCoverage.residue;
    result.wakeCoverage = wakeFoam;
    result.shoreCoverage = shoreFoam.coverage;
    result.surfaceFoam = lace * saturate(gFoamOpacity);
    result.bubbleHaze = bubbleHaze * saturate(gFoamOpacity) * kFoamBubbleOpacity;

    // 泡の色（Lambert 白 × 太陽直達 + 天空光）。明るさは泡の模様と同じ所で揺らす（ボロノイの網目なら高周波ノイズ・画像なら G）。
    // 水の中の気泡は、同じ光を気泡の雲の中を進む道のりの分だけ水の吸収で減らした色
    result.color = float3(0.0f, 0.0f, 0.0f);
    result.bubbleColor = float3(0.0f, 0.0f, 0.0f);
    if (result.mask > 0.0f || result.bubbles > 0.0f)
    {
        const float3 foamLight = ComputeFoamColor(normal, mainLightVisibility);
        const float shoreFoamShare = shoreFoam.coverage / max(openFoam + shoreFoam.coverage, 1.0e-4f);
        const float wakeFoamShare = wakeFoam / max(whitecapFoam + wakeFoam, 1.0e-4f);
        const float openGrain = lerp(whitecapGrain, wakeGrain, wakeFoamShare);
        const float foamGrain = lerp(openGrain, shoreGrain, shoreFoamShare);
        result.color = foamLight * foamGrain;
        result.bubbleColor = foamLight * kFoamBubbleBrightness
            * exp(-max(gAbsorptionCoeff, 0.0f) * kFoamBubblePathMeters);
    }
    return result;
}

#endif // WATER_FOAM_APPEARANCE_INCLUDED

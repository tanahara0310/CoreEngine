// ============================================================
// A-Trous ウェーブレットフィルタ by RTシャドウデノイザー（SVGF 準拠）
//
// 【原理】
//   間引き（A-Trous）サンプリングで実効カーネル半径を指数的に拡大する。
//   5x5 の B3 スプライン核を使う。
//   パス0: ステップ=1px  -> 実効 5x5
//   パス1: ステップ=2px  -> 実効 9x9
//   パス2: ステップ=4px  -> 実効 17x17
//
// 【重み付け】
//   - 深度差: 物体境界を保護
//   - 法線差: 面の向きが違うところへ影を漏らさない
//   - 影値差: 「その値がもつ推定分散」で正規化する（SVGF のエッジ停止関数）
//
//   旧実装は影値差の重みを固定の gPhiShadow で割っていた。固定値だと
//     ・ノイズが大きい場所（蓄積が浅い / ペナンブラ）では隣接差が閾値を超えて重みが消え、
//       ノイズがそのまま保存される（＝デノイザーがノイズを「エッジ」と誤認する）
//     ・ノイズが小さい場所では逆にぼかしすぎる
//   という二重の失敗をする。分散で正規化すると「ノイズ相当の差はぼかし、
//   ノイズでは説明できない差＝本物の影の境界だけ残す」という正しい判定になる。
//
// 【入出力レイアウト】RTShadowTemporal.CS.hlsl と共通
//   .x = シャドウ値 / .y = 推定分散 / .z = 蓄積フレーム数 N / .w = 線形ビュー深度
//   .z .w はこのパスでは加工せず素通しする（デバッグ表示と後段の一貫性のため）。
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gInputShadow : register(t0);     // トレース解像度
Texture2D<float4> gNormalRoughness : register(t1); // フル解像度
Texture2D<float> gSceneDepth : register(t2);       // フル解像度

RWTexture2D<float4> gOutputShadow : register(u0);  // トレース解像度

cbuffer DenoiseConstants : register(b0)
{
    int gStepSize;
    float gPhiShadow;  ///< エッジ停止関数の σ 倍率（大きいほど広くぼける）
    float gPhiNormal;  ///< 法線重みの指数
    float gPhiDepth;   ///< 深度重みの厳しさ
    int gTraceWidth;
    int gTraceHeight;
    // 深度重みに使う線形化パラメータ (proj._33, proj._43)。
    float gProjM33;
    float gProjM43;
    int gTraceScale; // 1 = フル解像度 / 2 = ハーフ解像度
    int gTraceOffsetX;
    int gTraceOffsetY;
    int gFullWidth;
    int gFullHeight;
    int gPad0_;
    int gPad1_;
    int gPad2_;
};

/// @brief トレース座標 → 対応するフル解像度ピクセル座標（G-Buffer 参照用）
/// @details 2x2 の固定代表点を使う。巡回オフセット（gTraceOffset）を使うと
///          ガイドが毎フレーム別ピクセルになり、静止シーンでも重みが揺れてちらつく。
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + (gTraceScale >> 1);
    return min(full, int2(gFullWidth - 1, gFullHeight - 1));
}

/// @brief 5x5 B3 スプライン核（外積で作れるので 1 次元で持つ）
static const float kKernel1D[5] = { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gTraceWidth || coord.y >= gTraceHeight)
        return;

    const float2 projZW = float2(gProjM33, gProjM43);
    float4 center = gInputShadow.Load(int3(coord, 0));

    float centerNdcDepth = gSceneDepth.Load(int3(TraceToFull(coord), 0));
    if (IsBackgroundDepth(centerNdcDepth))
    {
        gOutputShadow[coord] = center;
        return;
    }

    float3 centerNormal = normalize(gNormalRoughness.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);
    float centerDepth = LinearizeViewDepth(centerNdcDepth, projZW);
    float centerShadow = center.x;

    // ===== エッジ停止関数の σ =====
    //   分散をそのまま使うと分散自体のノイズで重みが暴れるので、SVGF と同じく
    //   3x3 ガウシアンで均してから平方根を取る。
    float varianceBlurred = 0.0f;
    {
        const float kGauss3[3] = { 0.25f, 0.5f, 0.25f };
        [unroll]
        for (int gy = -1; gy <= 1; ++gy)
        {
            [unroll]
            for (int gx = -1; gx <= 1; ++gx)
            {
                int2 gc = clamp(coord + int2(gx, gy),
                                int2(0, 0),
                                int2(gTraceWidth - 1, gTraceHeight - 1));
                varianceBlurred += gInputShadow.Load(int3(gc, 0)).y * kGauss3[gx + 1] * kGauss3[gy + 1];
            }
        }
    }
    // 下限を入れないと、収束しきった領域で σ→0 になりフィルタが完全停止する
    float sigmaShadow = gPhiShadow * sqrt(max(varianceBlurred, 0.0f)) + 1e-3f;

    float weightSum = 0.0f;
    float weightSqSum = 0.0f;
    float shadowSum = 0.0f;
    float varianceSum = 0.0f;

    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            int2 sampleCoord = clamp(
                coord + int2(dx, dy) * gStepSize,
                int2(0, 0),
                int2(gTraceWidth - 1, gTraceHeight - 1));
            int2 sampleFull = TraceToFull(sampleCoord);

            float sampleNdcDepth = gSceneDepth.Load(int3(sampleFull, 0));
            if (IsBackgroundDepth(sampleNdcDepth))
                continue;

            float3 sampleNormal = normalize(gNormalRoughness.Load(int3(sampleFull, 0)).rgb * 2.0f - 1.0f);
            float sampleDepth = LinearizeViewDepth(sampleNdcDepth, projZW);
            float4 sampleValue = gInputShadow.Load(int3(sampleCoord, 0));

            float w = kKernel1D[dy + 2] * kKernel1D[dx + 2];

            // 深度重み（物体境界保護）
            // 相対差をタップ距離で正規化して見る。絶対差 [m] のままだと遠景や
            // かすめ角の平面で全タップの重みが消え、デノイズが実質停止する。
            // タップ距離で割るのは、平面上では隣接差が距離に比例して増えるため
            // （割らないと後段パスほど平面がぼけなくなる）。
            float tapDistance = float(max(abs(dx), abs(dy)) * gStepSize);
            float relDepthDiff = abs(centerDepth - sampleDepth)
                               / (max(centerDepth, 1e-3f) * max(tapDistance, 1.0f));
            float wDepth = exp(-relDepthDiff * 16.0f * gPhiDepth);

            // 法線重み。旧実装はパスが進むほど指数を 4→8→16→32 と厳しくしていたが、
            // これは逆効果だった。ステップ幅が広い後段ほど離れたタップを見るので、
            // 曲面ではわずかな向きの差でも指数が効いて重みが消え、
            // 「低周波ノイズを均すはずの広いパスだけが働かない」状態になる。
            // 全パス共通の穏やかな指数にする。
            float wNormal = pow(max(0.0f, dot(centerNormal, sampleNormal)), gPhiNormal);

            // 影値重み（推定分散で正規化 = SVGF のエッジ停止関数）
            float diff = centerShadow - sampleValue.x;
            float wShadow = exp(-(diff * diff) / (2.0f * sigmaShadow * sigmaShadow));

            float totalWeight = w * wDepth * wNormal * wShadow;
            shadowSum += sampleValue.x * totalWeight;
            // 分散は重みの 2 乗で伝播する（線形結合の分散）
            varianceSum += sampleValue.y * totalWeight * totalWeight;
            weightSum += totalWeight;
            weightSqSum += totalWeight * totalWeight;
        }
    }

    float4 result = center;
    if (weightSum > 1e-6f)
    {
        result.x = shadowSum / weightSum;
        result.y = varianceSum / (weightSum * weightSum);
    }
    gOutputShadow[coord] = result;
}

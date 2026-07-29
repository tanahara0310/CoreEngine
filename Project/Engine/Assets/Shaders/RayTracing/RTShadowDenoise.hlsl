// ============================================================
// A-Trous ウェーブレットフィルタ by RTシャドウデノイザー
//
// 【原理】
//   間引き（A-Trous）サンプリングで実効カーネル半径を指数的に拡大する。
//   パス0: ステップ=1px  -> 実効3x3
//   パス1: ステップ=2px  -> 実効7x7
//   パス2: ステップ=4px  -> 実効15x15
//
// 【重み付け: ソフトシャドウ対応版】
//   - 深度差:    物体境界を保護（常に有効）
//   - 法線差:    パスが進むほど強く適用（近距離は深度のみ、遠距離は法線も）
//   - 影値差:    ペナンブラ（中間値）は広くブラー
//               -> ソフトシャドウ境界の滑らかさを維持
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float> gInputShadow : register(t0);      // トレース解像度
Texture2D<float4> gNormalRoughness : register(t1); // フル解像度
Texture2D<float> gSceneDepth : register(t2);       // フル解像度

RWTexture2D<float> gOutputShadow : register(u0);   // トレース解像度

cbuffer DenoiseConstants : register(b0)
{
    int gStepSize;
    float gPhiShadow;
    float gPhiNormal;
    float gPhiDepth;
    int gTraceWidth;
    int gTraceHeight;
    // 深度重みに使う線形化パラメータ (proj._33, proj._43)。
    // 以前は gInvViewProj(16 float) を受け取り、1 タップごとに 4x4 行列積で
    // ワールド座標を復元してから length() を取っていた（1px あたり 10 回 × 4 パス = 40 回）。
    // 深度差しか要らないので線形ビュー深度で十分。C++ 側 DenoiseConstants と要一致。
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
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + int2(gTraceOffsetX, gTraceOffsetY);
    return min(full, int2(gFullWidth - 1, gFullHeight - 1));
}

static const float kKernel[3][3] =
{
    { 1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f },
    { 2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f },
    { 1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f }
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gTraceWidth || coord.y >= gTraceHeight)
        return;

    const float2 projZW = float2(gProjM33, gProjM43);

    float centerNdcDepth = gSceneDepth.Load(int3(TraceToFull(coord), 0));
    if (IsBackgroundDepth(centerNdcDepth))
    {
        gOutputShadow[coord] = gInputShadow.Load(int3(coord, 0));
        return;
    }

    float3 centerNormal = normalize(gNormalRoughness.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);
    // 旧実装は length(worldPos)＝ワールド原点からの距離を深度扱いしていた。
    // 視線方向に沿っていないので、原点から等距離のシルエット両側でエッジを検出できない。
    // 線形ビュー深度に置き換えることで安くなり、かつ指標としても正しくなる。
    float centerDepth = LinearizeViewDepth(centerNdcDepth, projZW);
    float centerShadow = gInputShadow.Load(int3(coord, 0));

    float weightSum = 0.0f;
    float shadowSum = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
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
            float sampleShadow = gInputShadow.Load(int3(sampleCoord, 0));

            float w = kKernel[dy + 1][dx + 1];

            // 深度重み（物体境界保護）
            float wDepth = exp(-abs(centerDepth - sampleDepth) * gPhiDepth);

            // 法線重み（パスが進むほど強く適用）
            float wNormal = pow(max(0.0f, dot(centerNormal, sampleNormal)), gPhiNormal);

            // 影値重み（ペナンブラを広くブラー）
            float diff = abs(centerShadow - sampleShadow);
            float wShadow = exp(-(diff * diff) / (2.0f * gPhiShadow * gPhiShadow));

            float totalWeight = w * wDepth * wNormal * wShadow;
            shadowSum += sampleShadow * totalWeight;
            weightSum += totalWeight;
        }
    }

    gOutputShadow[coord] = (weightSum > 1e-6f) ? (shadowSum / weightSum) : centerShadow;
}

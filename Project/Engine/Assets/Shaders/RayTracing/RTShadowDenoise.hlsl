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

Texture2D<float> gInputShadow : register(t0);
Texture2D<float4> gNormalRoughness : register(t1);
Texture2D<float4> gWorldPosition : register(t2);

RWTexture2D<float> gOutputShadow : register(u0);

cbuffer DenoiseConstants : register(b0)
{
    int gStepSize;
    float gPhiShadow;
    float gPhiNormal;
    float gPhiDepth;
    int gScreenWidth;
    int gScreenHeight;
    float gPadding0;
    float gPadding1;
};

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
    if (coord.x >= gScreenWidth || coord.y >= gScreenHeight)
        return;

    float4 centerWorldPos = gWorldPosition.Load(int3(coord, 0));
    if (centerWorldPos.a < 0.5f)
    {
        gOutputShadow[coord] = gInputShadow.Load(int3(coord, 0));
        return;
    }

    float3 centerNormal = normalize(gNormalRoughness.Load(int3(coord, 0)).rgb * 2.0f - 1.0f);
    float centerDepth = length(centerWorldPos.xyz);
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
                int2(gScreenWidth - 1, gScreenHeight - 1));

            float4 sampleWorldPos = gWorldPosition.Load(int3(sampleCoord, 0));
            if (sampleWorldPos.a < 0.5f)
                continue;

            float3 sampleNormal = normalize(gNormalRoughness.Load(int3(sampleCoord, 0)).rgb * 2.0f - 1.0f);
            float sampleDepth = length(sampleWorldPos.xyz);
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

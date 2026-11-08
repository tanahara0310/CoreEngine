// RT デノイザの A-Trous ウェーブレットフィルタ（5x5 の B3 スプライン核をステップ幅を倍にしながら重ねる）

#include "../Include/Common/DepthReconstruction.hlsli"

// 入力：x = 値、y = 値の推定分散、z = 蓄積フレーム数 N（0 は信号の無い画素）、w = 線形ビュー深度 ※トレース解像度
Texture2D<float4> gInputSignal : register(t0);
Texture2D<float4> gNormalRoughness : register(t1); // フル解像度
Texture2D<float> gSceneDepth : register(t2);       // フル解像度

// 出力：入力と同じ並び（z と w は入力のまま）※トレース解像度
RWTexture2D<float4> gOutputSignal : register(u0);

cbuffer RTDenoiseATrousConstants : register(b0)
{
    int gStepSize;
    float gPhiValue;   ///< エッジ停止関数の σ 倍率（大きいほど広くぼける）
    float gPhiNormal;  ///< 法線重みの指数
    float gPhiDepth;   ///< 深度重みの厳しさ
    int gTraceWidth;
    int gTraceHeight;
    float gProjM33;    // 深度の線形化に使う proj._33
    float gProjM43;    // 深度の線形化に使う proj._43
    int gTraceScale;   // 1 = フル解像度 / 2 = ハーフ解像度
    int gPad0_;
    int gPad1_;
    int gFullWidth;
    int gFullHeight;
    int gPad2_;
    int gPad3_;
    int gPad4_;
};

/// @brief トレース座標 → G-Buffer を読むフル解像度の座標（2x2 の中央の固定の代表点）
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
    float4 center = gInputSignal.Load(int3(coord, 0));

    // 背景と信号の無い画素はそのまま通す
    float centerNdcDepth = gSceneDepth.Load(int3(TraceToFull(coord), 0));
    if (IsBackgroundDepth(centerNdcDepth) || center.z <= 0.0f)
    {
        gOutputSignal[coord] = center;
        return;
    }

    float3 centerNormal = normalize(gNormalRoughness.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);
    float centerDepth = LinearizeViewDepth(centerNdcDepth, projZW);
    float centerValue = center.x;

    // ===== エッジ停止関数の σ（分散を 3x3 のガウシアンで均してから平方根を取る）=====
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
                varianceBlurred += gInputSignal.Load(int3(gc, 0)).y * kGauss3[gx + 1] * kGauss3[gy + 1];
            }
        }
    }
    // 収束した所でも σ が 0 にならないよう下限を足す
    float sigmaValue = gPhiValue * sqrt(max(varianceBlurred, 0.0f)) + 1e-3f;

    float weightSum = 0.0f;
    float weightSqSum = 0.0f;
    float valueSum = 0.0f;
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
            float4 sampleValue = gInputSignal.Load(int3(sampleCoord, 0));
            if (sampleValue.z <= 0.0f)
                continue; // 信号の無い画素

            float w = kKernel1D[dy + 2] * kKernel1D[dx + 2];

            // 深度の重み（相対差をタップの距離で割って見る）
            float tapDistance = float(max(abs(dx), abs(dy)) * gStepSize);
            float relDepthDiff = abs(centerDepth - sampleDepth)
                               / (max(centerDepth, 1e-3f) * max(tapDistance, 1.0f));
            float wDepth = exp(-relDepthDiff * 16.0f * gPhiDepth);

            // 法線の重み（全パスで同じ指数）
            float wNormal = pow(max(0.0f, dot(centerNormal, sampleNormal)), gPhiNormal);

            // 値の重み（値の差を推定標準偏差で割って見る）
            float diff = centerValue - sampleValue.x;
            float wValue = exp(-(diff * diff) / (2.0f * sigmaValue * sigmaValue));

            float totalWeight = w * wDepth * wNormal * wValue;
            valueSum += sampleValue.x * totalWeight;
            // 分散は重みの 2 乗で伝える
            varianceSum += sampleValue.y * totalWeight * totalWeight;
            weightSum += totalWeight;
            weightSqSum += totalWeight * totalWeight;
        }
    }

    float4 result = center;
    if (weightSum > 1e-6f)
    {
        result.x = valueSum / weightSum;
        result.y = varianceSum / (weightSum * weightSum);
    }
    gOutputSignal[coord] = result;
}

// RT デノイザの仕上げ：トレース解像度の値を、深度と法線をガイドにフル解像度へ引き伸ばす（等倍なら写すだけ）

#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gTraceSignal : register(t0);     // トレース解像度の値（x）
Texture2D<float> gSceneDepth : register(t1);       // フル解像度の深度
Texture2D<float4> gNormalRoughness : register(t2); // フル解像度の法線

RWTexture2D<float> gOutputSignal : register(u0);   // フル解像度の出力

cbuffer RTDenoiseResolveConstants : register(b0)
{
    int gFullWidth;
    int gFullHeight;
    int gTraceWidth;
    int gTraceHeight;
    float gProjM33;    // 深度の線形化に使う proj._33
    float gProjM43;    // 深度の線形化に使う proj._43
    int gTraceScale;   // 1 = フル解像度 / 2 = ハーフ解像度
    int gPad0_;
    int gPad1_;
    float gPhiDepth;   // 深度重みの厳しさ
    int gPad2_;
    int gPad3_;
};

/// @brief トレース座標 → そのテクセルが代表するフル解像度の座標（2x2 の中央の固定の代表点）
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + (gTraceScale >> 1);
    return min(full, int2(gFullWidth - 1, gFullHeight - 1));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gFullWidth || coord.y >= gFullHeight)
        return;

    // 等倍のときは写すだけ
    if (gTraceScale == 1)
    {
        gOutputSignal[coord] = gTraceSignal.Load(int3(coord, 0)).x;
        return;
    }

    const float2 projZW = float2(gProjM33, gProjM43);

    // 背景は 1
    float centerNdcDepth = gSceneDepth.Load(int3(coord, 0));
    if (IsBackgroundDepth(centerNdcDepth))
    {
        gOutputSignal[coord] = 1.0f;
        return;
    }

    float centerDepth = LinearizeViewDepth(centerNdcDepth, projZW);
    float3 centerNormal = normalize(gNormalRoughness.Load(int3(coord, 0)).rgb * 2.0f - 1.0f);

    // ===== このフル解像度の画素を囲む 2x2 のトレース座標 =====
    //   トレース座標 t の代表点は t*scale + scale/2 なので、連続したトレース座標は (coord - scale/2) / scale。
    float2 continuousTrace = (float2(coord) - float(gTraceScale >> 1)) / float(gTraceScale);
    int2 baseTrace = int2(floor(continuousTrace));
    float2 subPixel = continuousTrace - float2(baseTrace);

    float valueSum = 0.0f;
    float weightSum = 0.0f;
    float fallback = 0.0f;
    float fallbackWeight = 0.0f;

    [unroll]
    for (int dy = 0; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = 0; dx <= 1; ++dx)
        {
            int2 traceCoord = clamp(baseTrace + int2(dx, dy),
                                    int2(0, 0),
                                    int2(gTraceWidth - 1, gTraceHeight - 1));
            int2 sourceFull = TraceToFull(traceCoord);

            float s = gTraceSignal.Load(int3(traceCoord, 0)).x;

            // バイリニアの重み
            float wSpatial = ((dx == 0) ? (1.0f - subPixel.x) : subPixel.x)
                           * ((dy == 0) ? (1.0f - subPixel.y) : subPixel.y);

            // 幾何が合うタップが無いときに使う、バイリニアの重みだけの平均
            fallback += s * wSpatial;
            fallbackWeight += wSpatial;

            float sNdcDepth = gSceneDepth.Load(int3(sourceFull, 0));
            if (IsBackgroundDepth(sNdcDepth))
                continue;

            float sDepth = LinearizeViewDepth(sNdcDepth, projZW);
            float3 sNormal = normalize(gNormalRoughness.Load(int3(sourceFull, 0)).rgb * 2.0f - 1.0f);

            // 深度は相対差で見る
            float relativeDepthDiff = abs(centerDepth - sDepth) / max(centerDepth, 1e-3f);
            float wDepth = exp(-relativeDepthDiff * gPhiDepth);
            float wNormal = pow(max(0.0f, dot(centerNormal, sNormal)), 16.0f);

            float w = wSpatial * wDepth * wNormal;
            valueSum += s * w;
            weightSum += w;
        }
    }

    // 幾何が合うタップが無いときは、バイリニアの重みだけの平均を使う
    float result = (weightSum > 1e-4f)
        ? (valueSum / weightSum)
        : ((fallbackWeight > 1e-4f) ? (fallback / fallbackWeight) : 1.0f);

    gOutputSignal[coord] = result;
}

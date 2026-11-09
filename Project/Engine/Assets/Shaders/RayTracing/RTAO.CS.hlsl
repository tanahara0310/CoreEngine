// ============================================================
// レイトレーシングの環境光の遮り（RTAO）
// 画素の点から法線のまわりの半球へ短いレイを撃ち、遮られなかった割合を書く。
// 出力はトレース解像度の (遮られなかった割合, 撃ったレイの本数)。ノイズ取りは RTDenoiser が受け持つ。
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"
#include "../Include/Common/Sampling.hlsli"
#include "../Include/RayTracing/RTTrace.hlsli"

cbuffer RTAOConstants : register(b0)
{
    float4x4 gInvViewProj;        // View * Projection の逆行列（深度からワールド座標を戻す）
    uint gFullWidth;              // 画面の幅
    uint gFullHeight;             // 画面の高さ
    uint gTraceWidth;             // トレース解像度の幅
    uint gTraceHeight;            // トレース解像度の高さ
    uint gTraceScale;             // 1 = フル解像度 / 2 = ハーフ解像度
    uint gFrameIndex;             // 乱数の列の番号
    uint gSampleCount;            // 1 画素に撃つレイの本数
    uint gNewPixelSampleCount;    // 履歴の浅い画素に撃つレイの本数
    float gRadius;                // レイの長さ [m]
    float gIntensity;             // 遮りの強さ（0 = 遮らない / 1 = そのまま）
    float gBias;                  // レイの始点を面から離す距離 [m]
    uint gHistoryValid;           // 1 なら前フレームの履歴を読む
    uint gHitInstanceTableIndex;  // 材質の表（kRTHitNoTexture = アルファを調べず、どの物も不透明として当てる）
    uint gHitSubMeshTableIndex;
    uint gPad0_;
    uint gPad1_;
};

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gNormalRoughness : register(t2);
// 前フレームの履歴（RTDenoiseTemporal.CS.hlsl の出力。z = 重ねたフレーム数）。同じ座標を読む
Texture2D<float4> gAOHistory : register(t3);
RWTexture2D<float2> gAOOutput : register(u0);

/// @brief 1 画素に撃つレイの本数の上限
static const uint kMaxSamples = 16;
/// @brief 重ねたフレーム数がこれ未満の画素を「現れた直後」とみなす
static const float kNewPixelHistoryFrames = 8.0f;
/// @brief D24_UNORM の深度の 1 コード
static const float kDepthQuantum = 1.0f / 16777216.0f;

/// @brief 当たった距離から遮りの重さを求める（始点で 1・レイの長さで 0）
float OcclusionAtDistance(float hitT)
{
    const float t = saturate(hitT / max(gRadius, 1.0e-4f));
    return 1.0f - t * t;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 traceCoord = dispatchThreadId.xy;
    if (any(traceCoord >= uint2(gTraceWidth, gTraceHeight)))
    {
        return;
    }

    // トレース解像度の画素が受け持つフル解像度の画素（RTDenoiser と同じ 2x2 の中央）
    const uint2 fullCoord = min(traceCoord * gTraceScale + (gTraceScale >> 1), uint2(gFullWidth - 1, gFullHeight - 1));

    const float ndcDepth = gSceneDepth.Load(int3(fullCoord, 0));
    if (IsBackgroundDepth(ndcDepth))
    {
        gAOOutput[traceCoord] = float2(1.0f, 1.0f);
        return;
    }

    const float2 screenUV = (float2(fullCoord) + 0.5f) / float2(gFullWidth, gFullHeight);
    const float2 ndc = ScreenUVToNDC(screenUV);
    const float3 worldPos = ReconstructWorldPosition(ndc, ndcDepth, gInvViewProj);
    const float3 N = normalize(gNormalRoughness.Load(int3(fullCoord, 0)).rgb * 2.0f - 1.0f);

    // 深度の 1 コードぶんのずれをワールドで測り、始点を面から離す距離に足す
    const float3 worldPosQuantized = ReconstructWorldPosition(ndc, max(ndcDepth - kDepthQuantum, 0.0f), gInvViewProj);
    const float bias = max(gBias, length(worldPos - worldPosQuantized) * 4.0f);
    const float3 origin = worldPos + N * bias;

    // 履歴の浅い画素（現れた直後）と履歴の無いフレームはレイを増やす
    uint sampleCount = gSampleCount;
    const bool newPixel = (gHistoryValid == 0)
        || (gAOHistory.Load(int3(traceCoord, 0)).z < kNewPixelHistoryFrames);
    if (newPixel)
    {
        sampleCount = max(sampleCount, gNewPixelSampleCount);
    }
    sampleCount = clamp(sampleCount, 1u, kMaxSamples);

    // 1 画素が覆う幅（葉などのアルファを調べるテクスチャのミップの選択に使う）
    const float2 neighborUV = screenUV + float2(float(gTraceScale) / float(gFullWidth), 0.0f);
    const float pixelFootprint = length(ReconstructWorldPosition(ScreenUVToNDC(neighborUV), ndcDepth, gInvViewProj) - worldPos);
    const RTSceneTables tables = MakeRTSceneTables(gHitInstanceTableIndex, gHitSubMeshTableIndex);

    float3 tangent;
    float3 bitangent;
    BuildOrthonormalBasis(N, tangent, bitangent);

    // 時間方向は R2 列で層化し、空間方向は画素ごとの回転でずらす
    const uint2 rotation = SampleRotation(traceCoord);
    float visibility = 0.0f;
    [loop]
    for (uint s = 0; s < sampleCount; ++s)
    {
        const uint2 fixedPoint = R2SequenceFixed(gFrameIndex * kMaxSamples + s) + rotation;
        const float2 xi = float2(fixedPoint) * (1.0f / 4294967296.0f);
        const float3 local = HemisphereSampleCosine(xi);

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = normalize(tangent * local.x + bitangent * local.y + N * local.z);
        ray.TMin = 0.001f;
        ray.TMax = gRadius;

        // 葉などアルファで抜ける所は通す
        const RTTraceHit hit = TraceRTClosestHit(gScene, tables, ray, kRTInstanceMaskShadowCaster, pixelFootprint, 0.0f);
        const float occlusion = hit.hit ? OcclusionAtDistance(hit.hitT) : 0.0f;
        visibility += 1.0f - gIntensity * occlusion;
    }

    gAOOutput[traceCoord] = float2(visibility / float(sampleCount), float(sampleCount));
}

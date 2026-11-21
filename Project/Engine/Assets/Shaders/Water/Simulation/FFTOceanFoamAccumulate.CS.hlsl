// ============================================================
// 白波の残りの泡の蓄積と減衰
// ------------------------------------------------------------
// カメラの周りの 2 段の格子（近く・遠く）に、砕けた白波の泡と物がかき回した所の泡を溜めて減らす。
//   x: 白波の水面の泡の被覆率 F = max(prev.x * exp(-dt/τ), injection)
//   y: 白波の水の中の気泡 B     = max(prev.y * exp(-dt/τB), injection)
//   w: 物がかき回した所の気泡 B = max(max(prev.w, wake) * exp(-dt/τB), wake)
//   z: 物がかき回した所の水面の泡の被覆率 F。気泡が浮いてきて泡になる:
//        F = (1 - (1 - prev.z) * exp(-∫B dt / τrise)) * exp(-dt/τ)
//      τrise が 0 のときは max(prev.z * exp(-dt/τ), wake)
// τ は泡の寿命に、泡と一緒に流れる位置のゆっくりした乱数を掛けたもの。τB は気泡が浮き切るまでの時間。
// 格子は変位前の参照格子座標を風下へ流した座標に置く。
// 白波の注入は水面の瞬間の泡と同じ判定（全カスケードの合成ヤコビアン × 波群エンベロープ）で行う。
// 1 フレームに進めるのは gActiveLevel の段だけで、ほかの段は前のフレームの値を写す。
// 物がかき回した所の泡（gWakeSources）は、どちらの段にも毎フレーム w の気泡へ max で重ねる。
// 源は描かれた水面の位置にあるので、格子の点を波の水平の変位で動かした位置で比べる。
// ============================================================

#include "../Common/WaterWhitecapCoverage.hlsli"
#include "../Common/WaveSurface.hlsli"

static const uint kWakeMaxSources = 64;

Texture2DArray<float4> gJacobian : register(t0);
Texture2DArray<float4> gFoamPrev : register(t1);
Texture2DArray<float4> gDisplacement : register(t2);
// 出力は全段を 1 ビューで見せる UAV（Dispatch z = 段）
RWTexture2DArray<float4> gFoamOutput : register(u0);
SamplerState gLinearWrap : register(s0);

// C++ 側 WaterWhitecapFoam::FoamConstants とレイアウト一致必須
cbuffer FFTOceanFoamConstants : register(b0)
{
    float4 gLevelOriginsXZ;     // xy = 近くの段・zw = 遠くの段の XZ の最小の角 [m]（風下へ流した座標）
    float4 gPrevLevelOriginsXZ; // 前のフレームの段ごとの角 [m]
    float2 gLevelTexelSizes;    // x = 近くの段・y = 遠くの段の 1 テクセルの幅 [m]
    uint gResolution;           // 段の一辺のテクセル数
    float gDeltaSeconds;        // 進める段を前に進めてから経ったシミュレーション時間 [s]
    float3 gFoamCascadeWeights; // カスケード別の勾配の重み（水面の瞬間の泡と同じ値）
    float gFoamBias;            // 砕けるしきい値（水面の瞬間の泡と同じ値）
    float3 gWaveGroupPhase;     // 波群エンベロープの位相のずれ [rad]
    float gFoamGain;            // しきい値からの立ち上がりの傾き
    float2 gDriftOffsetXZ;      // 泡が風下へ流れた距離 [m]
    float gFoamDecaySeconds;    // 泡の寿命 τ（e^-1 に減衰するまでの秒数）
    uint gResetFoam;            // 1 = 前のフレームを捨てる（全段を進める）
    uint gActiveLevel;          // このフレームに進める段
    uint gWakeSourceCount;      // かき回した所の泡の源の数
    float2 gWakeMinSigmas;      // 段ごとの源の幅の下限 [m]
    float gBubbleSeconds;       // 気泡が浮き切るまでの時間 τB [s]
    float gRiseSeconds;         // 物がかき回した所の気泡が水面の泡になる時間 τrise [s]（0 = 気泡と同時に泡を入れる）
    float2 gFoamConstantsPad;
    int4 gWakeRects[kWhitecapResidueLevelCount]; // 段ごとに源を入れるテクセルの範囲（xy = 最小・zw = 最大。最大は含まない）
    float4 gWakeSources[kWakeMaxSources];        // (x, z, σ [m], 水面を上下させた速さ [m/s])
};

/// @brief 位置ごとの寿命の倍率の範囲
static const float kWhitecapLifetimeScaleMin = 0.45f;
static const float kWhitecapLifetimeScaleMax = 1.55f;
/// @brief 寿命の倍率を変える乱数の細かさ [1/m]
static const float kWhitecapLifetimeNoiseFrequency = 0.11f;

/// @brief 源の幅に対する、泡を入れる範囲の比（WaterWhitecapFoam::kWakeReachSigmas と同じ値）
static const float kWakeReachSigmas = 3.0f;
/// @brief 水面を上下させた速さのうち、泡が出始める速さと、注入が 1 になる速さ [m/s]
static const float kWakeFoamStartRate = 0.4f;
static const float kWakeFoamFullRate = 1.6f;

/// @brief 格子の点に入れる、物がかき回した所の気泡の量 [0,1]
/// @details 源ごとに水面を上下させた速さをガウスで広げ、いちばん大きい速さを、泡が出始める速さから
///          1 になる速さまでの割合 s にする。気泡の量は s³（1 回だけ入れたときに浮いてくる泡の量が s² に近くなる）。
///          τrise が 0 のときは、泡へ直接入れる量 s² を返す
float ComputeWakeFoamInjection(int2 texel, uint level, float2 foamXZ, float texelSize)
{
    const int4 rect = gWakeRects[level];
    if (gWakeSourceCount == 0u || any(texel < rect.xy) || any(texel >= rect.zw))
    {
        return 0.0f;
    }

    const float2 baseXZ = foamXZ + gDriftOffsetXZ;
    const float2 drawnXZ = baseXZ
        + SampleFFTVertexDisplacement(gDisplacement, gLinearWrap, baseXZ, texelSize, gWaveGroupPhase).xz;
    const float minSigma = (level == 0u) ? gWakeMinSigmas.x : gWakeMinSigmas.y;

    float rate = 0.0f;
    for (uint i = 0; i < min(gWakeSourceCount, kWakeMaxSources); ++i)
    {
        const float4 source = gWakeSources[i];
        const float sigma = max(source.z, minSigma);
        const float2 offset = drawnXZ - source.xy;
        const float r2 = dot(offset, offset);
        if (r2 > kWakeReachSigmas * kWakeReachSigmas * sigma * sigma)
        {
            continue;
        }
        rate = max(rate, source.w * exp(-0.5f * r2 / (sigma * sigma)));
    }
    const float amount = saturate((rate - kWakeFoamStartRate) / (kWakeFoamFullRate - kWakeFoamStartRate));
    return (gRiseSeconds > 0.0f) ? amount * amount * amount : amount * amount;
}

/// @brief 物がかき回した所の水面の泡と気泡を 1 段進める
/// @param previous     前の (水面の泡 F, 気泡 B)
/// @param wake         この段で入れる気泡の量
/// @param decay        水面の泡の減衰 exp(-dt/τ)
/// @param bubbleDecay  気泡の減衰 exp(-dt/τB)
/// @return 進めた後の (水面の泡 F, 気泡 B)
/// @details 気泡は段の始めに入れてから減らし、その間に水面へ浮いた分だけ泡を増やす。
///          泡は (1 - F) に比例して増えるので 1 を超えない
float2 AdvanceObjectFoam(float2 previous, float wake, float decay, float bubbleDecay)
{
    const float bubbles = max(previous.y, wake);
    float foam;
    if (gRiseSeconds > 0.0f)
    {
        const float risenBubbles = bubbles * max(gBubbleSeconds, 1.0e-3f) * (1.0f - bubbleDecay);
        foam = (1.0f - (1.0f - previous.x) * exp(-risenBubbles / gRiseSeconds)) * decay;
    }
    else
    {
        foam = max(previous.x * decay, wake);
    }
    return float2(foam, max(bubbles * bubbleDecay, wake));
}

/// @brief 泡と一緒に流れる位置での、寿命の倍率
float ComputeWhitecapLifetimeScale(float2 foamXZ)
{
    const float noise = FoamValueNoise(foamXZ * kWhitecapLifetimeNoiseFrequency + float2(41.7f, -12.3f));
    return lerp(kWhitecapLifetimeScaleMin, kWhitecapLifetimeScaleMax, noise);
}

/// @brief 前のフレームで同じ位置にあった泡を読む
/// @details 前の範囲の外にはみ出た所は、近くの段なら前のフレームの遠くの段から読む。遠くの段は 0
float4 LoadPreviousFoam(float2 foamXZ, uint level, float texelSize, float2 prevOriginXZ)
{
    const int2 prevCoord = (int2)round((foamXZ - prevOriginXZ) / texelSize - 0.5f);
    if (all(prevCoord >= 0) && all(prevCoord < (int)gResolution))
    {
        return gFoamPrev.Load(int4(prevCoord, level, 0));
    }
    if (level == 0u)
    {
        const float2 farUV = (foamXZ - gPrevLevelOriginsXZ.zw) / (gLevelTexelSizes.y * (float)gResolution);
        if (all(farUV >= 0.0f) && all(farUV <= 1.0f))
        {
            return gFoamPrev.SampleLevel(gLinearWrap, float3(farUV, 1.0f), 0.0f);
        }
    }
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint level = min(dispatchThreadId.z, kWhitecapResidueLevelCount - 1u);
    const float texelSize = (level == 0u) ? gLevelTexelSizes.x : gLevelTexelSizes.y;
    const float2 originXZ = (level == 0u) ? gLevelOriginsXZ.xy : gLevelOriginsXZ.zw;
    const float2 foamXZ = originXZ + (float2(dispatchThreadId.xy) + 0.5f) * texelSize;
    const float wake = ComputeWakeFoamInjection(int2(dispatchThreadId.xy), level, foamXZ, texelSize);

    if (level != gActiveLevel && gResetFoam == 0u)
    {
        const float4 copied = gFoamPrev.Load(int4(dispatchThreadId.xy, level, 0));
        const float objectFoam = (gRiseSeconds > 0.0f) ? copied.z : max(copied.z, wake);
        gFoamOutput[dispatchThreadId] = float4(copied.xy, objectFoam, max(copied.w, wake));
        return;
    }

    const float2 prevOriginXZ = (level == 0u) ? gPrevLevelOriginsXZ.xy : gPrevLevelOriginsXZ.zw;
    const float4 previous = (gResetFoam != 0u)
        ? float4(0.0f, 0.0f, 0.0f, 0.0f)
        : LoadPreviousFoam(foamXZ, level, texelSize, prevOriginXZ);

    // 格子点の泡が今いる参照格子の位置で砕けを判定し、砕けの度合いの二乗を注入する。
    // シミュレーション時間が止まっている間は注入しない
    const float2 baseXZ = foamXZ + gDriftOffsetXZ;
    const float detJ = ComputeFFTCombinedDetJ(baseXZ, gJacobian, gLinearWrap, gFoamCascadeWeights, gWaveGroupPhase);
    const float breaking = ComputeWhitecapInstant(detJ, gFoamBias, gFoamGain);
    const float injection = (gDeltaSeconds > 0.0f) ? breaking * breaking : 0.0f;

    // 指数減衰。dt=0（ポーズ中）は decay=1 で凍結される
    const float lifetime = gFoamDecaySeconds * ComputeWhitecapLifetimeScale(foamXZ);
    const float decay = exp(-gDeltaSeconds / max(lifetime, 1.0e-3f));
    const float bubbleDecay = exp(-gDeltaSeconds / max(gBubbleSeconds, 1.0e-3f));

    gFoamOutput[dispatchThreadId] = float4(
        max(previous.x * decay, injection),
        max(previous.y * bubbleDecay, injection),
        AdvanceObjectFoam(previous.zw, wake, decay, bubbleDecay));
}

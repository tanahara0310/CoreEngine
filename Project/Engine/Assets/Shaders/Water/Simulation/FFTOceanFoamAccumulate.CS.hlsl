// ============================================================
// 白波の残りの泡の蓄積と減衰
// ------------------------------------------------------------
// カメラの周りの 2 段の格子（近く・遠く）に、砕けた白波の泡を溜めて減らす。
//   x: 白波の水面の泡の被覆率 F = max(F' * exp(-dt/τ), injection)
//   y: 白波の水の中の気泡 B     = max(B' * exp(-dt/τB), injection)
// F'・B' は前の値に、弱い拡散 κ∇²·dt を足したもの（塊がにじんで薄くなる）。
// τ は泡の寿命に、泡と一緒に流れる位置のゆっくりした乱数を掛けたもの。τB は気泡が浮き切るまでの時間。
// 格子は変位前の参照格子座標を風下へ流した座標に置く。
// 注入は水面の瞬間の泡と同じ判定（全カスケードの合成ヤコビアン × 波群エンベロープ）で行う。
// 1 フレームに進めるのは gActiveLevel の段だけで、ほかの段は前のフレームの値を写す。
// ============================================================

#include "../Common/WaterWhitecapCoverage.hlsli"

Texture2DArray<float4> gJacobian : register(t0);
Texture2DArray<float2> gFoamPrev : register(t1);
// 出力は全段を 1 ビューで見せる UAV（Dispatch z = 段）
RWTexture2DArray<float2> gFoamOutput : register(u0);
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
    float gBubbleSeconds;       // 気泡が浮き切るまでの時間 τB [s]
    float gSpreadCoefficient;   // 泡と気泡がにじむ速さ κ [m²/s]（0 = にじまない）
    float gFoamConstantsPad;
};

/// @brief 1 歩でにじませる割合 κ·dt/Δx² の上限（5 点の差分が安定な範囲）
static const float kMaxSpreadStep = 0.2f;

/// @brief 前のフレームで同じ位置にあった泡を読む
/// @details 前の範囲の外にはみ出た所は、近くの段なら前のフレームの遠くの段から読む。遠くの段は 0
float2 LoadPreviousFoam(float2 foamXZ, uint level, float texelSize, float2 prevOriginXZ)
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
    return float2(0.0f, 0.0f);
}

/// @brief 前のフレームの泡と気泡に、弱い拡散を 1 歩分足して読む
float2 LoadSpreadPreviousFoam(float2 foamXZ, uint level, float texelSize, float2 prevOriginXZ)
{
    const float2 center = LoadPreviousFoam(foamXZ, level, texelSize, prevOriginXZ);
    const float spread = min(gSpreadCoefficient * gDeltaSeconds / (texelSize * texelSize), kMaxSpreadStep);
    [branch]
    if (spread <= 0.0f)
    {
        return center;
    }
    const float2 neighbors =
        LoadPreviousFoam(foamXZ + float2(texelSize, 0.0f), level, texelSize, prevOriginXZ)
        + LoadPreviousFoam(foamXZ - float2(texelSize, 0.0f), level, texelSize, prevOriginXZ)
        + LoadPreviousFoam(foamXZ + float2(0.0f, texelSize), level, texelSize, prevOriginXZ)
        + LoadPreviousFoam(foamXZ - float2(0.0f, texelSize), level, texelSize, prevOriginXZ);
    return max(center + spread * (neighbors - 4.0f * center), 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint level = min(dispatchThreadId.z, kWhitecapResidueLevelCount - 1u);
    if (level != gActiveLevel && gResetFoam == 0u)
    {
        gFoamOutput[dispatchThreadId] = gFoamPrev.Load(int4(dispatchThreadId.xy, level, 0));
        return;
    }

    const float texelSize = (level == 0u) ? gLevelTexelSizes.x : gLevelTexelSizes.y;
    const float2 originXZ = (level == 0u) ? gLevelOriginsXZ.xy : gLevelOriginsXZ.zw;
    const float2 foamXZ = originXZ + (float2(dispatchThreadId.xy) + 0.5f) * texelSize;
    const float2 prevOriginXZ = (level == 0u) ? gPrevLevelOriginsXZ.xy : gPrevLevelOriginsXZ.zw;
    const float2 previous = (gResetFoam != 0u)
        ? float2(0.0f, 0.0f)
        : LoadSpreadPreviousFoam(foamXZ, level, texelSize, prevOriginXZ);

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

    gFoamOutput[dispatchThreadId] = float2(
        max(previous.x * decay, injection),
        max(previous.y * bubbleDecay, injection));
}

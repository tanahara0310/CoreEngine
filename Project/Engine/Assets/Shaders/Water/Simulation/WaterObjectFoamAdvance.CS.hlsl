// ============================================================
// 物がかき回した所の泡と、物が起こした水の流れを 1 歩進める
// ------------------------------------------------------------
// カメラの前に置いた 1 段の格子に、流れ u = (ux, uz) と泡 (F, B, 模様を動かす速さ vx, vz) を持つ。
// 格子の座標は FFT の水なら変位前の参照格子の座標、ほかの水なら世界の XZ。
// 流れ:
//   u ← u(x − (u + w)Δt) + ν∇²u·Δt（流れ自身と風下の流れ w で運び、にじませる）、その後 e^(−Δt/τu) で弱める。
//   源ごとに、外向きの成分を k × かき回す速さ × (r/σ)·e^(½ − r²/2σ²) まで上げ（外へ押す）、
//   ガウスの重みで物の水平の速さの kDragShare 倍へ寄せる（引きずる）。陸のマスは 0。
// 泡:
//   (F, B) を前の流れ u と風下の流れ w で半ラグランジュに運び、κ∇²·Δt でにじませ、
//   e^(−(∇·u)Δt) を掛ける（広がると薄く、集まると濃くなる。1 で頭打ち）。
//   気泡 B は源から入れ、浮いた分だけ水面の泡 F を増やす:
//     F = (1 − (1 − F)·e^(−∫B dt / τrise))·e^(−Δt/τ)（τrise が 0 なら F = max(F·e^(−Δt/τ), 入れた量)）
//   模様を動かす速さ v は流れ u を、模様の伸び縮みが上限に収まるまで遅くしたもの（ComputeObjectFoamPatternVelocity）。
// 源は描かれた水面の位置にあるので、FFT の水では格子の点を波の水平の変位で動かした位置で比べる。
// 格子の外は 0 として読む。
// ============================================================

#include "../Common/WaterWhitecapCoverage.hlsli"
#include "../Common/WaveSurface.hlsli"
#include "../Common/WaterSeaMap.hlsli"

/// @brief 格子の一辺のマス数（WaterObjectFoam::kResolution と同じ値）
static const uint kObjectFoamResolution = 1024;
/// @brief 1 フレームの源の数の上限（WaterObjectFoam::kMaxSources と同じ値）
static const uint kObjectFoamMaxSources = 64;

Texture2D<float4> gFoamPrev : register(t0);
Texture2D<float2> gFlowPrev : register(t1);
Texture2DArray<float4> gDisplacement : register(t2);
Texture2D<float4> gShoreMap : register(t3);
RWTexture2D<float4> gFoamOutput : register(u0);
RWTexture2D<float2> gFlowOutput : register(u1);
SamplerState gLinearClamp : register(s0);
SamplerState gLinearWrap : register(s1);

// C++ 側 WaterObjectFoamConstants とレイアウト一致必須
cbuffer WaterObjectFoamConstants : register(b0)
{
    float2 gOriginXZ;              // この歩の後の格子の最小の角 [m]
    float2 gPrevOriginXZ;          // 前の状態の格子の最小の角 [m]
    float gCellSize;               // 1 マスの幅 [m]
    float gDeltaSeconds;           // 1 歩の時間 [s]
    uint gClear;                   // 1 = 前の状態を 0 として読む
    uint gSourceCount;             // 源の数
    float2 gWindDriftVelocity;     // 風下の流れ [m/s]
    float gFlowStrength;           // 物が起こす流れの倍率（0 = 運ばない）
    float gDecaySeconds;           // 泡の寿命 τ [s]
    float2 gDriftOffsetXZ;         // この歩の後に泡が風下へ流れた距離 [m]
    float gBubbleSeconds;          // 気泡が浮き切るまでの時間 τB [s]
    float gRiseSeconds;            // 気泡が水面の泡になる時間 τrise [s]（0 = 気泡と同時に泡を入れる）
    float3 gWaveGroupPhase;        // 波群エンベロープの位相のずれ [rad]
    uint gUseDisplacement;         // 1 = 源を FFT の水平の変位で動かした位置で比べる
    int4 gSourceRect;              // 源を足すマスの範囲（xy = 最小・zw = 最大。最大は含まない）
    float2 gShoreMapOriginXZ;      // 海の範囲マップの最小の角 [m]
    float gShoreMapTexelsPerMeter; // 海の範囲マップの 1 m あたりのテクセル数
    float gShoreDepthOffset;       // 今の水面の高さ − 地図を測ったときの水面の高さ [m]
    uint2 gShoreMapResolution;     // 海の範囲マップのテクセル数（0 = 地図なし。陸を扱わない）
    float2 gShorePad;
    // 源ごとに 2 つ: (x, z, σ [m], 泡を入れる速さ [m/s]) と (vx, vz [m/s], かき回す速さ [m/s], 0)
    float4 gSources[kObjectFoamMaxSources * 2];
};

/// @brief 外へ押す流れの速さ ÷ かき回す速さ
static const float kPushStrength = 0.3f;
/// @brief 物の水平の速さへ流れを寄せる時間 [s]
static const float kDragSeconds = 0.3f;
/// @brief 引きずられた水の速さ ÷ 物の水平の速さ
static const float kDragShare = 0.25f;
/// @brief 流れが e^-1 に弱まる時間 τu [s]
static const float kFlowDampingSeconds = 1.0f;
/// @brief 流れがにじむ速さ ν [m²/s]
static const float kFlowViscosity = 0.05f;
/// @brief 泡と気泡がにじむ速さ κ [m²/s]
static const float kFoamDiffusion = 0.02f;
/// @brief 1 歩でにじませる割合 ν·Δt/Δx² の上限（5 点の差分が安定な範囲）
static const float kMaxViscosityStep = 0.2f;
/// @brief 源の幅に対する、源を足す範囲の比
static const float kSourceReachSigmas = 3.0f;
/// @brief 源の幅の下限（1 マスの幅に対する比）
static const float kMinSigmaCells = 0.75f;
/// @brief 泡を入れる速さのうち、泡が出始める速さと、注入が 1 になる速さ [m/s]
static const float kFoamStartRate = 0.4f;
static const float kFoamFullRate = 1.6f;

/// @brief 前の状態の格子の中の位置 [0,1]（範囲の外なら false）
bool ComputePrevUV(float2 positionXZ, out float2 uv)
{
    uv = (positionXZ - gPrevOriginXZ) / (gCellSize * (float)kObjectFoamResolution);
    return gClear == 0u && all(uv >= 0.0f) && all(uv <= 1.0f);
}

/// @brief 前の流れを双一次で読む（格子の外は 0）
float2 SamplePrevFlow(float2 positionXZ)
{
    float2 uv;
    return ComputePrevUV(positionXZ, uv) ? gFlowPrev.SampleLevel(gLinearClamp, uv, 0.0f) : float2(0.0f, 0.0f);
}

/// @brief 前の泡を双一次で読む（格子の外は 0）
float2 SamplePrevFoam(float2 positionXZ)
{
    float2 uv;
    return ComputePrevUV(positionXZ, uv) ? gFoamPrev.SampleLevel(gLinearClamp, uv, 0.0f).xy : float2(0.0f, 0.0f);
}

/// @brief 海の範囲マップのテクセルの、今の水面からの水深 [m]（水たまりは深い水として返す）
float LoadShoreDepth(int2 texel)
{
    const float depth = gShoreMap.Load(int3(texel, 0)).x;
    return IsWaterSeaMapBasin(depth) ? kWaterSeaMapOutsideValue : depth + gShoreDepthOffset;
}

/// @brief ワールド XZ の今の水面からの水深を、地図のテクセルの間で双一次に補間する [m]（地図の外は深い水）
float SampleShoreDepth(float2 worldXZ)
{
    const float2 texelPos = (worldXZ - gShoreMapOriginXZ) * gShoreMapTexelsPerMeter - 0.5f;
    if (any(texelPos < -0.5f) || any(texelPos > float2(gShoreMapResolution) - 0.5f))
    {
        return kWaterSeaMapOutsideValue;
    }
    const int2 maxCoord = int2(gShoreMapResolution) - 1;
    const int2 baseCoord = int2(floor(texelPos));
    const float2 f = texelPos - float2(baseCoord);
    const int2 p00 = clamp(baseCoord, 0, maxCoord);
    const int2 p11 = clamp(baseCoord + 1, 0, maxCoord);
    const float d00 = LoadShoreDepth(p00);
    const float d10 = LoadShoreDepth(int2(p11.x, p00.y));
    const float d01 = LoadShoreDepth(int2(p00.x, p11.y));
    const float d11 = LoadShoreDepth(p11);
    return lerp(lerp(d00, d10, f.x), lerp(d01, d11, f.x), f.y);
}

/// @brief 陸と水の中の物の下のマスか
bool IsLand(float2 positionXZ)
{
    return gShoreMapResolution.x != 0u && SampleShoreDepth(positionXZ) <= 0.0f;
}

/// @brief 気泡と水面の泡を 1 歩進める
/// @param previous    運んだ後の (水面の泡 F, 気泡 B)
/// @param injection   この歩で入れる気泡の量
/// @param decay       水面の泡の減衰 exp(-dt/τ)
/// @param bubbleDecay 気泡の減衰 exp(-dt/τB)
/// @return 進めた後の (水面の泡 F, 気泡 B)
/// @details 気泡は歩の始めに入れてから減らし、その間に水面へ浮いた分だけ泡を増やす。
///          泡は (1 - F) に比例して増えるので 1 を超えない
float2 AdvanceObjectFoam(float2 previous, float injection, float decay, float bubbleDecay)
{
    const float bubbles = max(previous.y, injection);
    float foam;
    if (gRiseSeconds > 0.0f)
    {
        const float risenBubbles = bubbles * max(gBubbleSeconds, 1.0e-3f) * (1.0f - bubbleDecay);
        foam = (1.0f - (1.0f - previous.x) * exp(-risenBubbles / gRiseSeconds)) * decay;
    }
    else
    {
        foam = max(previous.x * decay, injection);
    }
    return float2(foam, max(bubbles * bubbleDecay, injection));
}

/// @brief 源を流れへ足し、この点へ入れる気泡の量を返す
/// @param drawnXZ 格子の点が描かれる位置
/// @param flow    流れ（外へ押す・引きずる分を足して返す）
/// @return 入れる気泡の量 [0,1]（泡を入れる速さを、泡が出始める速さから 1 になる速さまでの割合 s にして、
///         τrise が 0 より大きいときは s³、0 のときは s²）
float ApplySources(float2 drawnXZ, inout float2 flow)
{
    const float dragBlend = 1.0f - exp(-gDeltaSeconds / kDragSeconds);
    const float minSigma = kMinSigmaCells * gCellSize;
    float rate = 0.0f;
    for (uint i = 0; i < min(gSourceCount, kObjectFoamMaxSources); ++i)
    {
        const float4 shape = gSources[i * 2u];
        const float4 motion = gSources[i * 2u + 1u];
        const float sigma = max(shape.z, minSigma);
        const float2 offset = drawnXZ - shape.xy;
        const float r2 = dot(offset, offset);
        if (r2 > kSourceReachSigmas * kSourceReachSigmas * sigma * sigma)
        {
            continue;
        }
        const float q = r2 / (sigma * sigma);
        const float gauss = exp(-0.5f * q);
        rate = max(rate, shape.w * gauss);

        [branch]
        if (gFlowStrength > 0.0f)
        {
            const float r = sqrt(r2);
            if (r > 1.0e-4f)
            {
                const float2 outward = offset / r;
                const float push = kPushStrength * gFlowStrength * motion.z * (r / sigma) * exp(0.5f - 0.5f * q);
                flow += outward * max(push - dot(flow, outward), 0.0f);
            }
            flow += (motion.xy * (kDragShare * gFlowStrength) - flow) * (dragBlend * gauss);
        }
    }
    const float amount = saturate((rate - kFoamStartRate) / (kFoamFullRate - kFoamStartRate));
    return (gRiseSeconds > 0.0f) ? amount * amount * amount : amount * amount;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= kObjectFoamResolution))
    {
        return;
    }

    const int2 cell = int2(dispatchThreadId.xy);
    const float2 positionXZ = gOriginXZ + (float2(cell) + 0.5f) * gCellSize;
    const float dt = gDeltaSeconds;
    const bool land = IsLand(positionXZ);

    // 前の流れ。この点と四方から、広がり ∇·u と ∇²u を中央差分で求める
    const float2 flowCenter = SamplePrevFlow(positionXZ);
    const float2 flowEast = SamplePrevFlow(positionXZ + float2(gCellSize, 0.0f));
    const float2 flowWest = SamplePrevFlow(positionXZ - float2(gCellSize, 0.0f));
    const float2 flowNorth = SamplePrevFlow(positionXZ + float2(0.0f, gCellSize));
    const float2 flowSouth = SamplePrevFlow(positionXZ - float2(0.0f, gCellSize));
    const float divergence = 0.5f * ((flowEast.x - flowWest.x) + (flowNorth.y - flowSouth.y)) / gCellSize;
    const float2 flowGradientX = 0.5f * (flowEast - flowWest) / gCellSize;
    const float2 flowGradientZ = 0.5f * (flowNorth - flowSouth) / gCellSize;
    const float flowGradientNorm = sqrt(dot(flowGradientX, flowGradientX) + dot(flowGradientZ, flowGradientZ));
    const float2 neighborSum = flowEast + flowWest + flowNorth + flowSouth;

    // 流れを運んでにじませ、弱める
    float2 flow = SamplePrevFlow(positionXZ - (flowCenter + gWindDriftVelocity) * dt);
    const float viscosityStep = min(kFlowViscosity * dt / (gCellSize * gCellSize), kMaxViscosityStep);
    flow += viscosityStep * (neighborSum - 4.0f * flowCenter);
    flow *= exp(-dt / kFlowDampingSeconds);

    // 源（外へ押す・引きずる・気泡を入れる）
    float injection = 0.0f;
    [branch]
    if (gSourceCount > 0u && all(cell >= gSourceRect.xy) && all(cell < gSourceRect.zw))
    {
        float2 drawnXZ = positionXZ;
        if (gUseDisplacement != 0u)
        {
            drawnXZ += SampleFFTVertexDisplacement(gDisplacement, gLinearWrap, positionXZ, gCellSize, gWaveGroupPhase).xz;
        }
        injection = ApplySources(drawnXZ, flow);
    }
    if (land || gFlowStrength <= 0.0f)
    {
        flow = float2(0.0f, 0.0f);
    }

    // 泡を前の流れと風下の流れで運んでにじませ、広がった分だけ薄く、集まった分だけ濃くする
    const float2 foamCenter = SamplePrevFoam(positionXZ);
    const float2 foamNeighborSum = SamplePrevFoam(positionXZ + float2(gCellSize, 0.0f))
        + SamplePrevFoam(positionXZ - float2(gCellSize, 0.0f))
        + SamplePrevFoam(positionXZ + float2(0.0f, gCellSize))
        + SamplePrevFoam(positionXZ - float2(0.0f, gCellSize));
    const float diffusionStep = min(kFoamDiffusion * dt / (gCellSize * gCellSize), kMaxViscosityStep);
    float2 foam = SamplePrevFoam(positionXZ - (flowCenter + gWindDriftVelocity) * dt);
    foam += diffusionStep * (foamNeighborSum - 4.0f * foamCenter);
    foam = saturate(foam * exp(-divergence * dt));

    // 気泡 → 水面の泡。寿命の乱数は風下へ流れる位置で決める
    const float lifetime = gDecaySeconds * ComputeWhitecapLifetimeScale(positionXZ - gDriftOffsetXZ);
    const float decay = exp(-dt / max(lifetime, 1.0e-3f));
    const float bubbleDecay = exp(-dt / max(gBubbleSeconds, 1.0e-3f));
    foam = AdvanceObjectFoam(foam, injection, decay, bubbleDecay);
    if (land)
    {
        foam = float2(0.0f, 0.0f);
    }

    gFoamOutput[cell] = float4(foam, ComputeObjectFoamPatternVelocity(flow, flowGradientNorm));
    gFlowOutput[cell] = flow;
}

// ============================================================
// 岸の泡の時間変化
// ============================================================

#include "../Common/FFTOceanCascade.hlsli"

Texture2D<float> gSeabedHeight : register(t0);
Texture2DArray<float4> gFFTOceanDisplacement : register(t1);
Texture2D<float4> gShoreFoamPrev : register(t2);
Texture2D<float2> gSwashDisplacement : register(t3);
RWTexture2D<float4> gShoreFoamOutput : register(u0);
SamplerState gLinearClamp : register(s0);
SamplerState gLinearWrap : register(s1);

cbuffer WaterShoreFoamConstants : register(b0)
{
    float2 gWindowOriginXZ;     // 範囲の XZ の最小の角 [m]（海底の高さと同じ範囲）
    float2 gPrevWindowOriginXZ; // 前のフレームの範囲の角 [m]
    float gWindowSize;          // 範囲の一辺 [m]
    float gTexelSize;           // 泡の格子の 1 テクセルの幅 [m]
    uint gResolution;           // 泡の格子の一辺のテクセル数
    float gWaterRestHeight;     // 静水面の高さ [m]
    float gDeltaSeconds;        // 前のフレームから進んだ時間 [s]
    float gDecaySeconds;        // 泡の寿命 τ [s]（e^-1 になるまでの時間）
    uint gResetFoam;            // 1 = 前のフレームの泡を捨てる
    float gShoreFoamPad;
    float3 gWaveGroupPhase;     // 波群エンベロープの位相のずれ [rad]
    float gShoreFoamPad2;
};

/// @brief 波が砕ける波高と静水深の比（H / h）
static const float kBreakingIndex = 0.78f;
/// @brief 砕け始めから泡が満量になるまでの H / h の幅
static const float kBreakingRamp = 0.39f;
/// @brief これより浅い静水深では、水があれば遡上として泡にする [m]
static const float kSwashStillDepth = 0.05f;
/// @brief 水があるとみなす最小の水深 [m]
static const float kWetDepth = 0.01f;
/// @brief 砕けた所の泡の被覆率
static const float kShoreFoamStrength = 0.9f;

/// @brief 水面の上下（静水面からの高さ）[m]。水面メッシュの頂点と同じ式
float SampleWaveElevation(float2 worldXZ)
{
    float height = 0.0f;
    [unroll]
    for (int c = 0; c < kFFTGeometryCascadeCount; ++c)
    {
        const float2 cuv = ComputeFFTCascadeUV(worldXZ, c);
        height += gFFTOceanDisplacement.SampleLevel(gLinearWrap, float3(cuv, (float)c), 0.0f).y;
    }
    return height * ComputeFFTWaveGroupEnvelope(worldXZ, gWaveGroupPhase);
}

/// @brief 海底の高さ（ワールド Y）[m]
float SampleSeabedHeight(float2 worldXZ)
{
    return gSeabedHeight.SampleLevel(gLinearClamp, (worldXZ - gWindowOriginXZ) / gWindowSize, 0.0f);
}

/// @brief 静止位置の水の粒が寄せ・引きで今ずれている量 [m]（WaterShoreSwash.CS が求めたもの）
float2 SampleSwashDisplacement(float2 restXZ)
{
    return gSwashDisplacement.SampleLevel(gLinearClamp, (restXZ - gWindowOriginXZ) / gWindowSize, 0.0f);
}

/// @brief 今の位置で波が砕けている度合い [0,1]
/// @details 波高（水面の上がりの 2 倍）が静水深の kBreakingIndex 倍を超えると砕ける。
///          静水深がごく浅い所と静水面より上は、水が来ていれば遡上の泡にする
float ComputeBreaking(float2 worldXZ)
{
    const float elevation = SampleWaveElevation(worldXZ);
    const float stillDepth = gWaterRestHeight - SampleSeabedHeight(worldXZ);
    if (stillDepth + elevation <= kWetDepth)
    {
        return 0.0f;
    }
    if (stillDepth <= kSwashStillDepth)
    {
        return 1.0f;
    }
    return saturate((2.0f * elevation / stillDepth - kBreakingIndex) / kBreakingRamp);
}

/// @brief 水の粒の静止位置（寄せ・引きでずれる前の位置）ごとに泡を 1 フレーム進める
/// @details 粒の今の位置（静止位置＋寄せ・引きのずれ）で波が砕けていれば泡を足し、寿命で減らす。
///          出力は (泡の被覆率, 寄せ・引きのずれ x, z, 0)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const float2 restXZ = gWindowOriginXZ + (float2(dispatchThreadId.xy) + 0.5f) * gTexelSize;
    const float2 swash = SampleSwashDisplacement(restXZ);
    const float breaking = ComputeBreaking(restXZ + swash);

    // 同じ静止位置の前のフレームの泡（範囲の角はテクセルの格子に揃っている）
    float previous = 0.0f;
    if (gResetFoam == 0)
    {
        const int2 previousCoord = (int2)round((restXZ - gPrevWindowOriginXZ) / gTexelSize - 0.5f);
        if (all(previousCoord >= 0) && all(previousCoord < (int)gResolution))
        {
            previous = gShoreFoamPrev.Load(int3(previousCoord, 0)).r;
        }
    }

    const float decay = exp(-gDeltaSeconds / max(gDecaySeconds, 1.0e-3f));
    const float foam = max(previous * decay, breaking * kShoreFoamStrength);
    gShoreFoamOutput[dispatchThreadId.xy] = float4(foam, swash, 0.0f);
}

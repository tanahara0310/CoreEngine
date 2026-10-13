// ============================================================
// 岸の泡の時間変化
// ============================================================

#include "../Common/FFTOceanCascade.hlsli"
#include "../Common/WaterShoreWave.hlsli"

Texture2D<float> gSeabedHeight : register(t0);
Texture2DArray<float4> gFFTOceanDisplacement : register(t1);
Texture2D<float4> gShoreFoamPrev : register(t2);
Texture2D<float2> gSwashDisplacement : register(t3);
Texture2D<float4> gShoreWaveField : register(t4);
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
    uint gShoreWaveEnabled;             // 1 = 岸の波の砕けを泡にする（gShoreWaveField を読む）
    float gShoreWaveAngularFrequency;   // 岸の波の ω [rad/s]
    float gShoreWavePhaseTime;          // 岸の波の時刻 [s]
    float gShoreWaveHeight;             // 沖での岸の波の波高 [m]
    float gShoreWaveStartDepth;         // 岸の波が来始める水深 [m]
    float gShoreWaveHeightVariation;    // 岸の波の 1 本ごとの波高のばらつき
    float gShoreWaveEdgeMargin;         // 範囲の端から内側へ、岸の波を 0 から満量へ移す幅 [m]
    float gShoreFoamPad3;
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
/// @brief 岸の波が砕けている峰の前の面で泡を足す幅（1 周期に対する割合）
static const float kShoreWaveFaceBand = 0.12f;
/// @brief 岸の波が砕けている峰の後ろで泡を足す幅（1 周期に対する割合）
static const float kShoreWaveCrestBand = 0.18f;
/// @brief 岸の波の砕けで足す泡の被覆率の上限
static const float kShoreWaveFoamStrength = 1.0f;

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

/// @brief b0 の岸の波の設定（場の範囲は泡の格子と同じ）
ShoreWaveParams GetFoamShoreWaveParams()
{
    ShoreWaveParams p;
    p.enabled = gShoreWaveEnabled != 0;
    p.originXZ = gWindowOriginXZ;
    p.size = gWindowSize;
    p.edgeMargin = gShoreWaveEdgeMargin;
    p.angularFrequency = gShoreWaveAngularFrequency;
    p.height = gShoreWaveHeight;
    p.startDepth = gShoreWaveStartDepth;
    p.heightVariation = gShoreWaveHeightVariation;
    return p;
}

/// @brief 今の位置で FFT の波が砕けている度合い [0,1]
/// @details 波高（水面の上がりの 2 倍）が静水深の kBreakingIndex 倍を超えると砕ける。
///          静水深がごく浅い所と静水面より上は、水が来ていれば遡上の泡にする
/// @param fftScale 浅い所で FFT の波を弱める倍率（岸の波が受け持つ分）
float ComputeBreaking(float2 worldXZ, float fftScale)
{
    const float elevation = SampleWaveElevation(worldXZ) * fftScale;
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

/// @brief 岸の波が砕けている峰の上と前の面に足す泡の被覆率 [0,1]
/// @details 峰の前の面（位相が 1 の手前）と峰の後ろ（位相が 0 の直後）で、砕けている度合いに応じて足す
float ComputeShoreWaveFoam(ShoreWaveResult shore)
{
    if (shore.breaking <= 0.0f)
    {
        return 0.0f;
    }
    const float face = 1.0f - saturate((1.0f - shore.phase) / kShoreWaveFaceBand);
    const float crest = 1.0f - saturate(shore.phase / kShoreWaveCrestBand);
    return kShoreWaveFoamStrength * shore.breaking * max(face, crest * crest);
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

    // 岸の波（砕けている峰で泡を足し、浅い所では FFT の波を弱める）
    float shoreWaveFoam = 0.0f;
    float fftScale = 1.0f;
    if (gShoreWaveEnabled != 0)
    {
        const ShoreWaveResult shore =
            EvaluateShoreWave(gShoreWaveField, GetFoamShoreWaveParams(), restXZ, gShoreWavePhaseTime);
        shoreWaveFoam = ComputeShoreWaveFoam(shore);
        fftScale = shore.fftScale;
    }
    const float breaking = ComputeBreaking(restXZ + swash, fftScale);

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
    const float foam = max(previous * decay, max(breaking * kShoreFoamStrength, shoreWaveFoam));
    gShoreFoamOutput[dispatchThreadId.xy] = float4(foam, swash, 0.0f);
}

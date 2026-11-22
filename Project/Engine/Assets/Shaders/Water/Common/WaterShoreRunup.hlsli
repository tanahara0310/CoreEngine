// ============================================================
// 浜の遡上（砕けた波が浜を駆け上がって引く水の膜）
// ------------------------------------------------------------
// 海の範囲マップ（WaterSeaMap.hlsli）の水深と、その傾きから、岸の細かい格子の頂点を持ち上げる高さを返す。
//   - 浅瀬: 砕けた波が段（ボア）になって岸へ進む。水深 d から水際へ着くまでの時間は
//           浅い水の波の速さ √(g·d) を坂 s で積分した 2√d / (s√g)。峰は等深線に沿うので浜と平行になる
//   - 水際: 波ごとに水際の高さが投げ上げの形で上がって下がる。水面が浜の砂より高い所だけ水に見える
//   - 波ごとの着く時刻と高さを、浜に沿ったなめらかな模様で変える（不規則に寄せる）
// 利用側: FFTWater.VS / RTWaterSurfaceCommon.hlsli（同じ式で形をそろえる）
// ============================================================
#ifndef WATER_SHORE_RUNUP_HLSLI
#define WATER_SHORE_RUNUP_HLSLI

#include "WaterSeaMap.hlsli"

/// @brief 波が水際へ着く平均の間隔 [s]
static const float kRunupPeriodSeconds = 9.0f;
/// @brief 着く時刻のばらつき（間隔に対する割合・±半分）
static const float kRunupArrivalJitter = 0.3f;
/// @brief 1 回の遡上が上がって引くまでの長さ（間隔に対する割合）
static const float kRunupDurationRatio = 0.8f;
/// @brief いちばん高く上がる時刻（1 回の遡上の長さに対する割合）
static const float kRunupPeakRatio = 0.35f;
/// @brief 波ごとの高さと時刻を変える模様の細かさ [1/m]
static const float kRunupAlongShoreFrequency = 1.0f / 28.0f;
/// @brief 波ごとの高さの幅（平均の高さに掛ける最小と最大）
static const float kRunupMinHeightScale = 0.3f;
static const float kRunupMaxHeightScale = 1.6f;
/// @brief 遡上の高さを平均の高さにする浜の傾き（これより緩い浜ほど低く上がる）
static const float kRunupReferenceSlope = 0.05f;
/// @brief 水際の水の持ち上がりを沖へ延ばす水深 [m]
static const float kRunupLensDepth = 0.8f;
/// @brief 砕けた波が段になって進む水深の上限 [m]（これより深い所は FFT の波だけ）
static const float kRunupBoreDepth = 1.6f;
/// @brief 段の高さ ÷ 水深
static const float kRunupBoreHeightRatio = 0.4f;
/// @brief 段の前の面が立ち上がる時間 [s]
static const float kRunupBoreRiseSeconds = 0.8f;
/// @brief 水深の傾きを測る間隔 [m]
static const float kRunupGradientStep = 3.0f;
/// @brief 傾きの下限
static const float kRunupMinSlope = 0.01f;
/// @brief 重力加速度 [m/s²]
static const float kRunupGravity = 9.81f;

/// @brief 2 次元の整数格子の点の乱数 [0, 1)
float RunupHash(float2 p)
{
    const float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    const float3 r = q + dot(q, q.yzx + 33.33f);
    return frac((r.x + r.y) * r.z);
}

/// @brief なめらかな値のノイズ [0, 1)
float RunupValueNoise(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = RunupHash(cell);
    const float b = RunupHash(cell + float2(1.0f, 0.0f));
    const float c = RunupHash(cell + float2(0.0f, 1.0f));
    const float d = RunupHash(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

/// @brief n 番目の波の、その場所での水際へ着く時刻 [s] と高さの倍率
void GetRunupWave(int waveIndex, float2 worldXZ, out float arrivalSeconds, out float heightScale)
{
    const float n = (float)waveIndex;
    const float2 p = worldXZ * kRunupAlongShoreFrequency + float2(n * 17.13f, n * 7.71f);
    const float jitter = kRunupArrivalJitter * (RunupValueNoise(p) - 0.5f);
    arrivalSeconds = (n + jitter) * kRunupPeriodSeconds;
    heightScale = lerp(kRunupMinHeightScale, kRunupMaxHeightScale, RunupValueNoise(p + float2(31.7f, 53.1f)));
}

/// @brief 水際の高さの形（0..1。上がって引く投げ上げの形）
/// @param u 遡上が始まってからの時間 ÷ 1 回の遡上の長さ
float RunupShorelineShape(float u)
{
    if (u <= 0.0f || u >= 1.0f)
    {
        return 0.0f;
    }
    const float x = (u < kRunupPeakRatio)
        ? (kRunupPeakRatio - u) / kRunupPeakRatio
        : (u - kRunupPeakRatio) / (1.0f - kRunupPeakRatio);
    return 1.0f - x * x;
}

/// @brief 段（ボア）の形（0..1。前の面で立ち上がり、次の波までに下がる）
/// @param ageSeconds 段の前の面が通ってからの時間 [s]
float RunupBoreShape(float ageSeconds)
{
    const float lifetime = kRunupDurationRatio * kRunupPeriodSeconds;
    if (ageSeconds <= 0.0f || ageSeconds >= lifetime)
    {
        return 0.0f;
    }
    return smoothstep(0.0f, kRunupBoreRiseSeconds, ageSeconds) * (1.0f - ageSeconds / lifetime);
}

/// @brief 岸の格子の頂点を持ち上げる高さ [m]
/// @param seaMap          海の範囲マップ
/// @param worldXZ         頂点の変位前のワールド XZ [m]
/// @param originXZ        地図の範囲の XZ の最小の角 [m]
/// @param texelsPerMeter  地図の 1m あたりのテクセル数
/// @param resolution      地図の X・Z のテクセル数
/// @param depthOffset     今の水面の高さ − 測ったときの水面の高さ [m]
/// @param timeSeconds     波の時刻 [s]
/// @param meanHeight      水際が上がる高さの平均 [m]（0 なら持ち上げない）
float EvaluateShoreRunupLift(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution,
    float depthOffset, float timeSeconds, float meanHeight)
{
    if (meanHeight <= 0.0f || resolution.x == 0)
    {
        return 0.0f;
    }

    const float4 center = SampleWaterSeaMap(seaMap, worldXZ, originXZ, texelsPerMeter, resolution);
    if (IsWaterSeaMapBasin(center.x))
    {
        return 0.0f;
    }
    const float depth = center.x + depthOffset;
    const float clearance = center.y - depthOffset;
    if (depth > kRunupBoreDepth || depth < -2.0f * kRunupMaxHeightScale * meanHeight)
    {
        return 0.0f;
    }

    // 水深の傾き（浜の坂）
    const float step = kRunupGradientStep;
    const float2 gradient = float2(
        SampleWaterSeaMap(seaMap, worldXZ + float2(step, 0.0f), originXZ, texelsPerMeter, resolution).x
            - SampleWaterSeaMap(seaMap, worldXZ - float2(step, 0.0f), originXZ, texelsPerMeter, resolution).x,
        SampleWaterSeaMap(seaMap, worldXZ + float2(0.0f, step), originXZ, texelsPerMeter, resolution).x
            - SampleWaterSeaMap(seaMap, worldXZ - float2(0.0f, step), originXZ, texelsPerMeter, resolution).x)
        / (2.0f * step);
    const float slope = clamp(length(gradient), kRunupMinSlope, 1.0f);
    const float slopeScale = saturate(slope / kRunupReferenceSlope);
    const float shoreHeight = meanHeight * slopeScale;

    // 水際へ着くまでの時間（浅瀬だけ。陸の上は 0）
    const float wetDepth = max(depth, 0.0f);
    const float travelSeconds = 2.0f * sqrt(wetDepth) / (slope * sqrt(kRunupGravity));

    const float lensWeight = saturate(1.0f - wetDepth / kRunupLensDepth);
    const float boreWeight = (1.0f - smoothstep(0.6f * kRunupBoreDepth, kRunupBoreDepth, wetDepth)) * slopeScale;
    const float duration = kRunupDurationRatio * kRunupPeriodSeconds;

    float lift = 0.0f;
    // 近い 3 つの波のうち、いちばん高い水面を使う
    const int boreWave = (int)floor((timeSeconds + travelSeconds) / kRunupPeriodSeconds);
    const int shoreWave = (int)floor(timeSeconds / kRunupPeriodSeconds);
    [unroll]
    for (int i = -1; i <= 1; ++i)
    {
        float arrival;
        float heightScale;

        // 浅瀬を進む段
        GetRunupWave(boreWave + i, worldXZ, arrival, heightScale);
        const float boreHeight = kRunupBoreHeightRatio * wetDepth * min(heightScale, 1.0f) * boreWeight;
        lift = max(lift, boreHeight * RunupBoreShape(timeSeconds - (arrival - travelSeconds)));

        // 水際の上がり下がり（沖へ lensWeight で薄める）
        GetRunupWave(shoreWave + i, worldXZ, arrival, heightScale);
        const float shoreline = shoreHeight * heightScale * RunupShorelineShape((timeSeconds - arrival) / duration);
        lift = max(lift, shoreline * lensWeight);
    }

    // 桟橋の床などの上の物より上へは上げない
    return min(lift, max(clearance - 0.05f, 0.0f));
}

#endif // WATER_SHORE_RUNUP_HLSLI

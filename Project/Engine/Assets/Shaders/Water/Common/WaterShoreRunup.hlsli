// ============================================================
// 岸の波（沖から寄せる波・砕けて岸へ進む段・浜を駆け上がって引く水の膜）
// ------------------------------------------------------------
// 海の範囲マップ（WaterSeaMap.hlsli）の水深と、岸へ寄せる波が沖から着くまでの時間の場（SampleShoreWaveField）から、
// 水面の頂点を持ち上げる高さを返す。n 番目の波は、場所 x を時刻 (n 番目の波が沖を出る時刻 + 沖から x まで着く時間) に通る。
// 波は同じ番号どうしで、沖・浅瀬・水際がつながる。
//   - 沖から寄せる波: 水深 kShoreWaveMaxDepth より浅い所で、峰が着く時間の等しい線に沿って岸へ進む。
//                     浅くなるほど遅く（浜と平行に向きをそろえる）、高く（水深の -1/4 乗）、峰が細く、前の面が急になる
//   - 砕けて進む段: 波の高さが水深の kWaterBreakingIndex 倍に届いた所から、段（ボア）になって岸へ進む
//   - 水際: 波が着くと、水際の高さが投げ上げの形で上がって下がる。水面が浜の砂より高い所だけ水に見える。
//           届く高さと上がり始める時刻は、水際に沿った位置の長さの違うノイズで波ごとに変わる（水際が弧の形に出入りする）。
//           ノイズは頂点を水深 0 の線へ寄せた位置で引く（浜を上がる向きには同じ値になる）
//   - 波は 2 つの列で寄せる。主なうねりと、少し斜めから来る低いうねり（峰に沿って進むほど遅れて着く）
//   - 波ごとの沖を出る時刻と高さを、波を運んできた沖の点の峰に沿った位置で変える（峰が曲がり、ところどころ低く途切れる）。
//     主なうねりは、大きい波と小さい波が組になって来る。ばらつきは列ごとに決まった数の波でくり返す
// 利用側: FFTWater.VS / RTWaterSurfaceCommon.hlsli（同じ式で形をそろえる）
// ============================================================
#ifndef WATER_SHORE_RUNUP_HLSLI
#define WATER_SHORE_RUNUP_HLSLI

#include "WaterSeaMap.hlsli"

/// @brief 1 回の遡上が上がって引くまでの長さ（波の間隔に対する割合）
static const float kRunupDurationRatio = 0.8f;
/// @brief いちばん高く上がる時刻（1 回の遡上の長さに対する割合）
static const float kRunupPeakRatio = 0.35f;
/// @brief 水際が上がる高さの、平均に対する上限の倍率（√kShoreWaveMaxHeightScale × (1 + kRunupReachVariation) 以上。WaterSystem.cpp と同じ値）
static const float kRunupMaxHeightScale = 2.2f;
/// @brief 水際の届く高さと上がり始める時刻を水際に沿って変えるノイズの数と、それぞれの長さ [m] と重み（重みの和は 1）
static const int kRunupEdgeOctaves = 2;
static const float kRunupEdgeNoiseMeters[2] = { 20.0f, 9.0f };
static const float kRunupEdgeNoiseWeights[2] = { 0.65f, 0.35f };
/// @brief 水際のノイズの重み付きの和の標準偏差（これで割って ±2 で切り、2 で割って [-1, 1] にする）
static const float kRunupEdgeNoiseStdDev = 0.159f;
/// @brief 水際の届く高さのばらつき（平均に対する ± の割合）
static const float kRunupReachVariation = 0.3f;
/// @brief 水際が上がり始める時刻のばらつき（1 回の遡上の長さに対する ± の割合）
static const float kRunupEdgeDelayVariation = 0.1f;
/// @brief 水際のノイズを引く位置を水深 0 の線へ寄せるときの、浜の傾きの下限
static const float kRunupEdgeMinSlope = 0.02f;
/// @brief 水際のノイズを引く位置を水深 0 の線へ寄せる距離の上限 [m]
static const float kRunupEdgeMaxShift = 40.0f;
/// @brief 水際のノイズの乱数の列の始まり（GetShoreWave の列と重ならない）
static const float kRunupEdgeHashRow = 128.0f;
/// @brief 遡上の高さを平均の高さにする浜の傾き（これより緩い浜ほど低く上がる）
static const float kRunupReferenceSlope = 0.05f;
/// @brief 水際の水の持ち上がりを沖へ延ばす水深 [m]
static const float kRunupLensDepth = 0.8f;
/// @brief 砕けた波が段になって進む水深の上限 [m]（沖から寄せる波が無いとき）
static const float kRunupBoreDepth = 1.6f;
/// @brief 砕けた波が段になって進む水深の上限 [m]（沖から寄せる波があるとき。砕けた波だけが段になる）
static const float kRunupBoreDepthWithShoreWaves = 3.0f;
/// @brief 段の高さ ÷ 水深
static const float kRunupBoreHeightRatio = 0.4f;
/// @brief 段の前の面が立ち上がる時間 [s]
static const float kRunupBoreRiseSeconds = 1.2f;
/// @brief 水深の傾きを測る間隔 [m]
static const float kRunupGradientStep = 3.0f;
/// @brief 傾きの下限
static const float kRunupMinSlope = 0.01f;
/// @brief 重力加速度 [m/s²]
static const float kRunupGravity = 9.81f;
/// @brief 2π
static const float kRunupTwoPi = 6.2831853f;

/// @brief 沖から寄せる波を描く水深の上限 [m]（これより深い所は FFT の波だけ）
static const float kShoreWaveMaxDepth = 12.0f;
/// @brief 沖から寄せる波をそのままの高さで描く水深の上限 [m]
static const float kShoreWaveFullDepth = 7.0f;
/// @brief 峰の幅（周期に対する割合。水深 kShoreWaveMaxDepth と、砕ける直前）
static const float kShoreWaveCrestWidthOffshore = 0.15f;
static const float kShoreWaveCrestWidthBreaking = 0.07f;
/// @brief 砕ける直前の峰の前後の幅の偏り（前の面を細く・後ろの面を広く）
static const float kShoreWaveCrestSkew = 0.45f;
/// @brief 波の高さ ÷ (水深 × kWaterBreakingIndex) がこの間で、寄せる波から段へ移る
static const float kShoreWaveBreakStartRatio = 0.85f;
static const float kShoreWaveBreakEndRatio = 1.15f;
/// @brief 峰の幅を、頂点の格子の間隔の何倍より細くしないか
static const float kShoreWaveMinCrestWidthSpacings = 1.5f;
/// @brief √π
static const float kShoreWaveSqrtPi = 1.7724539f;

/// @brief 峰に沿った高さの倍率（ノイズの値が kShoreWaveSectionLowNoise 以下で最小・kShoreWaveSectionHighNoise 以上で最大）
static const float kShoreWaveSectionMinScale = 0.12f;
static const float kShoreWaveSectionMaxScale = 1.5f;
static const float kShoreWaveSectionLowNoise = 0.3f;
static const float kShoreWaveSectionHighNoise = 0.7f;
/// @brief 波ごとの高さの倍率の幅
static const float kShoreWavePerWaveMinScale = 0.85f;
static const float kShoreWavePerWaveMaxScale = 1.15f;
/// @brief 主なうねりの、大きい波と小さい波の組の高さの倍率（kShoreWaveMainCycle 個の波の並び）
static const float kShoreWaveSetEnvelope[16] = {
    0.6f, 0.75f, 1.0f, 1.3f, 1.4f, 1.15f, 0.85f, 0.7f, 0.8f, 1.0f, 1.2f, 1.05f, 0.8f, 0.65f, 0.6f, 0.7f };
/// @brief 高さの倍率の上限（kShoreWaveSetEnvelope の最大 × kShoreWavePerWaveMaxScale × kShoreWaveSectionMaxScale。WaterSystem.cpp と同じ値）
static const float kShoreWaveMaxHeightScale = 2.42f;

/// @brief 岸へ寄せる波の列の数（0 = 主なうねり・1 = 少し斜めから来る低いうねり）
static const int kShoreWaveTrainCount = 2;
/// @brief 主なうねりの、波ごとのばらつきがくり返す波の数（間隔 × この数 = kShoreWaveFieldCycleSeconds）
static const int kShoreWaveMainCycle = 16;
/// @brief 斜めから来るうねりの、波ごとのばらつきがくり返す波の数（間隔 × この数 = kShoreWaveFieldCycleSeconds）
static const int kShoreWaveCrossCycle = 13;

/// @brief 岸へ寄せる波の列
struct ShoreWaveTrain
{
    float periodSeconds;  ///< 波の間隔 [s]
    int cycle;            ///< 波ごとのばらつきがくり返す波の数
    float delayPerMeter;  ///< 峰に沿った位置が 1m 進むごとに遅れて着く時間 [s/m]
    float heightRatio;    ///< 高さの倍率
    float sectionMeters;  ///< 着く時刻と高さを変える、峰に沿った長さ [m]
    float arrivalJitter;  ///< 着く時刻のばらつき（間隔に対する割合・±半分）
    float hashRow;        ///< 乱数の列の始まり
};

/// @brief 岸へ寄せる波の列 train の決まり
ShoreWaveTrain GetShoreWaveTrain(int train)
{
    ShoreWaveTrain w;
    if (train == 0)
    {
        w.periodSeconds = kShoreWaveFieldCycleSeconds / (float)kShoreWaveMainCycle;
        w.cycle = kShoreWaveMainCycle;
        w.delayPerMeter = 0.0f;
        w.heightRatio = 1.0f;
        w.sectionMeters = 32.0f;
        w.arrivalJitter = 0.35f;
        w.hashRow = 0.0f;
    }
    else
    {
        w.periodSeconds = kShoreWaveFieldCycleSeconds / (float)kShoreWaveCrossCycle;
        w.cycle = kShoreWaveCrossCycle;
        w.delayPerMeter = 0.03f;
        w.heightRatio = 0.65f;
        w.sectionMeters = 28.0f;
        w.arrivalJitter = 0.3f;
        w.hashRow = 64.0f;
    }
    return w;
}

/// @brief 2 次元の整数格子の点の乱数 [0, 1)
float RunupHash(float2 p)
{
    const float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    const float3 r = q + dot(q, q.yzx + 33.33f);
    return frac((r.x + r.y) * r.z);
}

/// @brief 乱数の列 row の、1 次元のなめらかな値のノイズ [0, 1)
float RunupValueNoise(float x, float row)
{
    const float cell = floor(x);
    const float f = x - cell;
    const float u = f * f * (3.0f - 2.0f * f);
    return lerp(RunupHash(float2(cell, row)), RunupHash(float2(cell + 1.0f, row)), u);
}

/// @brief 波の列 train の n 番目の波の、峰に沿った位置での沖を出る時刻 [s] と高さの倍率
/// @param crestMeters 波を運んできた沖の点の峰に沿った位置 [m]（SampleShoreWaveField の w）
void GetShoreWave(int train, int waveIndex, float crestMeters, out float departureSeconds, out float heightScale)
{
    const ShoreWaveTrain w = GetShoreWaveTrain(train);
    const int cycleIndex = ((waveIndex % w.cycle) + w.cycle) % w.cycle;
    const float row = w.hashRow + (float)cycleIndex;
    const float x = crestMeters / w.sectionMeters;

    const float jitter = w.arrivalJitter * (RunupValueNoise(x, row) - 0.5f);
    departureSeconds = ((float)waveIndex + jitter) * w.periodSeconds;

    const float section = lerp(kShoreWaveSectionMinScale, kShoreWaveSectionMaxScale,
        smoothstep(kShoreWaveSectionLowNoise, kShoreWaveSectionHighNoise, RunupValueNoise(x + 0.5f, row + 32.0f)));
    const float perWave = lerp(kShoreWavePerWaveMinScale, kShoreWavePerWaveMaxScale, RunupHash(float2(row, 211.0f)));
    const float set = (train == 0) ? kShoreWaveSetEnvelope[cycleIndex] : 1.0f;
    heightScale = w.heightRatio * set * perWave * section;
}

/// @brief 格子の点 cell の、乱数で決めた向きと、点からのずれ offset の内積
float RunupGradientCorner(float2 cell, float2 offset)
{
    float s;
    float c;
    sincos(kRunupTwoPi * RunupHash(cell), s, c);
    return c * offset.x + s * offset.y;
}

/// @brief 乱数の列 row の、2 次元のなめらかな傾きのノイズ（格子の点で 0。およそ [-0.7, 0.7]）
float RunupGradientNoise2D(float2 p, float row)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    const float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float2 c = cell + float2(row * 41.0f, row * 29.0f);
    const float n00 = RunupGradientCorner(c, f);
    const float n10 = RunupGradientCorner(c + float2(1.0f, 0.0f), f - float2(1.0f, 0.0f));
    const float n01 = RunupGradientCorner(c + float2(0.0f, 1.0f), f - float2(0.0f, 1.0f));
    const float n11 = RunupGradientCorner(c + float2(1.0f, 1.0f), f - float2(1.0f, 1.0f));
    return lerp(lerp(n00, n10, u.x), lerp(n01, n11, u.x), u.y);
}

/// @brief ワールド XZ での、長さの違うノイズの重み付きの和を [-1, 1] にした値
float RunupEdgeNoise(float2 worldXZ, float row)
{
    float sum = 0.0f;
    [unroll]
    for (int i = 0; i < kRunupEdgeOctaves; ++i)
    {
        sum += kRunupEdgeNoiseWeights[i] * RunupGradientNoise2D(worldXZ / kRunupEdgeNoiseMeters[i], row + 16.0f * (float)i);
    }
    return 0.5f * clamp(sum / kRunupEdgeNoiseStdDev, -2.0f, 2.0f);
}

/// @brief 頂点を水深 0 の線へ寄せたワールド XZ（水深の傾きの向きへ、水深 ÷ 傾き だけ動かす）
/// @param depth     頂点の、今の水面からの水深 [m]（陸の上は負）
/// @param gradient  水深の傾き（∂水深/∂x, ∂水深/∂z）
float2 ComputeRunupEdgePosition(float2 worldXZ, float depth, float2 gradient)
{
    const float gradientLength = max(length(gradient), kRunupEdgeMinSlope);
    const float shift = clamp(-depth / gradientLength, -kRunupEdgeMaxShift, kRunupEdgeMaxShift);
    return worldXZ + shift * gradient / gradientLength;
}

/// @brief 波の列 train の n 番目の波が水際を上がるときの、水際の位置での高さの倍率と遅れ
/// @param edgeXZ      頂点を水深 0 の線へ寄せたワールド XZ（ComputeRunupEdgePosition）
/// @param reachScale  水際の届く高さの倍率
/// @param delayRatio  上がり始める時刻の遅れ（1 回の遡上の長さに対する割合）
void GetRunupEdge(int train, int waveIndex, float2 edgeXZ, out float reachScale, out float delayRatio)
{
    const ShoreWaveTrain w = GetShoreWaveTrain(train);
    const int cycleIndex = ((waveIndex % w.cycle) + w.cycle) % w.cycle;
    const float row = kRunupEdgeHashRow + w.hashRow + (float)cycleIndex;
    reachScale = 1.0f + kRunupReachVariation * RunupEdgeNoise(edgeXZ, row);
    delayRatio = kRunupEdgeDelayVariation * RunupEdgeNoise(edgeXZ, row + 48.0f);
}

/// @brief 沖から寄せる波の、水深 depth での高さ ÷ (水深 × kWaterBreakingIndex)（1 を超えると砕けている）
float ComputeShoreWaveBreakingRatio(float shoreWaveHeight, float heightScale, float depth)
{
    const float safeDepth = max(depth, 0.01f);
    const float shoaledHeight = shoreWaveHeight * heightScale * pow(kShoreWaveMaxDepth / safeDepth, 0.25f);
    return shoaledHeight / (kWaterBreakingIndex * safeDepth);
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
/// @param lifetime   段が消えるまでの時間 [s]
float RunupBoreShape(float ageSeconds, float lifetime)
{
    if (ageSeconds <= 0.0f || ageSeconds >= lifetime)
    {
        return 0.0f;
    }
    return smoothstep(0.0f, kRunupBoreRiseSeconds, ageSeconds) * (1.0f - ageSeconds / lifetime);
}

/// @brief 沖から寄せる波（砕ける前）の、静水面からの高さと傾き
/// @param depth         今の水面からの水深 [m]
/// @param clearance     今の水面から上の物までの高さ [m]（これより上へは上げない）
/// @param field         SampleShoreWaveField の値
/// @param timeSeconds   波の時刻 [s]
/// @param shoreWaveHeight   水深 kShoreWaveMaxDepth での波の高さの平均 [m]（0 なら寄せる波なし）
/// @param vertexSpacing 頂点の格子の間隔 [m]
/// @return (高さ [m], ∂高さ/∂x, ∂高さ/∂z)
float3 EvaluateShoreWave(
    float depth, float clearance, float4 field, float timeSeconds, float shoreWaveHeight, float vertexSpacing)
{
    if (shoreWaveHeight <= 0.0f || depth <= 0.0f || depth >= kShoreWaveMaxDepth)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float offshoreSeconds = field.x;
    const float offshoreWeight = 1.0f - smoothstep(kShoreWaveFullDepth, kShoreWaveMaxDepth, depth);
    const float speed = sqrt(kRunupGravity * depth);
    const float minWidthSeconds = kShoreWaveMinCrestWidthSpacings * vertexSpacing / speed;

    float height = 0.0f;
    float slopeAlongTravel = 0.0f;
    float meanHeight = 0.0f;
    [unroll]
    for (int train = 0; train < kShoreWaveTrainCount; ++train)
    {
        const ShoreWaveTrain w = GetShoreWaveTrain(train);
        const float arrivalSeconds = offshoreSeconds + w.delayPerMeter * field.w;
        const int wave = (int)floor((timeSeconds - arrivalSeconds) / w.periodSeconds);
        // 近い 3 つの波の峰を重ねる
        [unroll]
        for (int i = -1; i <= 1; ++i)
        {
            float departure;
            float heightScale;
            GetShoreWave(train, wave + i, field.w, departure, heightScale);
            const float breakingRatio = ComputeShoreWaveBreakingRatio(shoreWaveHeight, heightScale, depth);
            const float unbroken = 1.0f - smoothstep(kShoreWaveBreakStartRatio, kShoreWaveBreakEndRatio, breakingRatio);
            const float crestHeight = min(breakingRatio, 1.0f) * kWaterBreakingIndex * depth * unbroken * offshoreWeight;

            // 砕ける直前ほど峰が細く、前の面（峰より先に通る面）が急になる
            const float steepness = saturate(breakingRatio);
            const float width = max(
                w.periodSeconds * lerp(kShoreWaveCrestWidthOffshore, kShoreWaveCrestWidthBreaking, steepness), minWidthSeconds);
            const float frontWidth = width * (1.0f - kShoreWaveCrestSkew * steepness);
            const float backWidth = width * (1.0f + kShoreWaveCrestSkew * steepness);
            const float age = timeSeconds - (departure + arrivalSeconds);
            const float sideWidth = (age < 0.0f) ? frontWidth : backWidth;
            const float x = age / sideWidth;
            const float shape = exp(-x * x);
            height += crestHeight * shape;
            slopeAlongTravel += crestHeight * shape * (-2.0f * x / sideWidth);
            meanHeight += crestHeight * 0.5f * (frontWidth + backWidth) * kShoreWaveSqrtPi / (3.0f * w.periodSeconds);
        }
    }
    // 1 周期の平均が静水面になるよう、峰の分の平均を引く
    height -= meanHeight;

    const float ceiling = max(clearance - 0.05f, 0.0f);
    if (height > ceiling)
    {
        return float3(ceiling, 0.0f, 0.0f);
    }
    // 峰が通ってからの時間は (時刻 − 沖から着く時間) で進むので、場所での傾きは沖から着く時間の傾きの逆向き
    return float3(height, -slopeAlongTravel * field.yz);
}

/// @brief 岸の格子の頂点を持ち上げる高さ [m]（砕けて進む段と水際の上がり下がり）
/// @param seaMap          海の範囲マップ
/// @param worldXZ         頂点の変位前のワールド XZ [m]
/// @param originXZ        地図の範囲の XZ の最小の角 [m]
/// @param texelsPerMeter  地図の 1m あたりのテクセル数
/// @param resolution      地図の X・Z のテクセル数
/// @param depthOffset     今の水面の高さ − 測ったときの水面の高さ [m]
/// @param timeSeconds     波の時刻 [s]
/// @param meanHeight      水際が上がる高さの平均 [m]（0 なら持ち上げない）
/// @param shoreWaveHeight     沖から寄せる波の高さの平均 [m]（0 なら、どの波も水深 kRunupBoreDepth から段になる）
float EvaluateShoreRunupLift(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution,
    float depthOffset, float timeSeconds, float meanHeight, float shoreWaveHeight)
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
    const bool withShoreWaves = shoreWaveHeight > 0.0f;
    const float boreDepth = withShoreWaves ? kRunupBoreDepthWithShoreWaves : kRunupBoreDepth;
    if (depth > boreDepth || depth < -2.0f * kRunupMaxHeightScale * meanHeight)
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
    const float2 edgeXZ = ComputeRunupEdgePosition(worldXZ, depth, gradient);

    // 沖から着くまでの時間（陸の上は、いちばん近い水際の値）
    const float wetDepth = max(depth, 0.0f);
    const float4 field = SampleShoreWaveField(seaMap, worldXZ, originXZ, texelsPerMeter, resolution);
    const float offshoreSeconds = field.x;

    const float lensWeight = saturate(1.0f - wetDepth / kRunupLensDepth);
    const float boreWeight = (1.0f - smoothstep(0.6f * boreDepth, boreDepth, wetDepth)) * slopeScale;

    float lift = 0.0f;
    [unroll]
    for (int train = 0; train < kShoreWaveTrainCount; ++train)
    {
        const ShoreWaveTrain w = GetShoreWaveTrain(train);
        const float arrivalSeconds = offshoreSeconds + w.delayPerMeter * field.w;
        const float duration = kRunupDurationRatio * w.periodSeconds;
        const int wave = (int)floor((timeSeconds - arrivalSeconds) / w.periodSeconds);
        // 近い 4 つの波（2 つ前から 1 つ後まで）のうち、いちばん高い水面を使う
        [unroll]
        for (int i = -2; i <= 1; ++i)
        {
            float departure;
            float heightScale;
            GetShoreWave(train, wave + i, field.w, departure, heightScale);
            const float age = timeSeconds - (departure + arrivalSeconds);

            // 浅瀬を進む段（沖から寄せる波があるときは、その場所で砕けている波だけ）
            const float broken = withShoreWaves
                ? smoothstep(kShoreWaveBreakStartRatio, kShoreWaveBreakEndRatio, ComputeShoreWaveBreakingRatio(shoreWaveHeight, heightScale, wetDepth))
                : 1.0f;
            const float boreHeight = kRunupBoreHeightRatio * wetDepth * min(heightScale, 1.0f) * boreWeight * broken;
            lift = max(lift, boreHeight * RunupBoreShape(age, duration));

            // 水際の上がり下がり（高さは波の高さの平方根で増え、水際に沿った位置と波ごとに変わる。沖へ lensWeight で薄める）
            float reachScale;
            float delayRatio;
            GetRunupEdge(train, wave + i, edgeXZ, reachScale, delayRatio);
            const float shoreline =
                shoreHeight * sqrt(heightScale) * reachScale * RunupShorelineShape(age / duration - delayRatio);
            lift = max(lift, shoreline * lensWeight);
        }
    }

    // 桟橋の床などの上の物より上へは上げない
    return min(lift, max(clearance - 0.05f, 0.0f));
}

#endif // WATER_SHORE_RUNUP_HLSLI

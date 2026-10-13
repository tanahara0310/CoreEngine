// ============================================================
// 岸の波（岸へ寄せて、浅くなるほど高く・前のめりになり、砕ける波）
// ------------------------------------------------------------
// 波の形は時刻から式で求める。式が読む「場」は WaterShoreWaveSystem が範囲（海底の高さと同じ）に作る
//   (到着時刻 T [s], ∂T/∂x, ∂T/∂z, ならした静水深 h [m])
// 到着時刻は、沖からうねりの向きに進む平面波が、水深で決まる速さで届く時刻。
// 水深 startDepth より深い所と、範囲の上流側の辺では平面波そのものの時刻とし、そこから浅い所へ解き進める。
// 位相 ω(τ − T) が同じ点を結んだ線が波の峰になる（浅い所ほど遅いので峰は岸と平行へ曲がる）。
// 利用側: WaterShoreWave*.CS（場を作る）/ FFTWater.VS / Water.PS / RTWaterSurfaceCommon
// ============================================================
#ifndef WATER_SHORE_WAVE_INCLUDED
#define WATER_SHORE_WAVE_INCLUDED

#include "WaterShoreMesh.hlsli"

static const float kShoreWavePi = 3.14159265f;
static const float kShoreWaveGravity = 9.81f;
/// @brief 波が砕ける波高と静水深の比（H / h。WaterShoreFoam.CS と同じ値）
static const float kShoreWaveBreakingIndex = 0.78f;
/// @brief 波の速さを求めるときの最小の水深 [m]
static const float kShoreWaveMinDepth = 0.05f;
/// @brief まだ波が届いていない到着時刻
static const float kShoreWaveUnreached = 1.0e9f;
/// @brief 波が届かない所（陸・ほかの水域から切れた水たまり）に書く水深 [m]（陸として扱う）
static const float kShoreWaveUnreachedDepth = -10.0f;
/// @brief 範囲の辺を上流側とみなす、うねりの向きと辺の内向きの法線の内積の下限
static const float kShoreWaveUpwindEdge = 0.3f;

/// @brief 線形分散関係 ω² = g k tanh(k h) を満たす波数 k [rad/m]（Fenton と McKee の近似式）
float ShoreWaveNumber(float angularFrequency, float depth)
{
    const float deepWaveNumber = angularFrequency * angularFrequency / kShoreWaveGravity;
    const float h = max(depth, kShoreWaveMinDepth);
    return deepWaveNumber / pow(tanh(pow(deepWaveNumber * h, 0.75f)), 2.0f / 3.0f);
}

/// @brief 波の速さ（位相速度）[m/s]
float ShoreWavePhaseSpeed(float angularFrequency, float depth)
{
    return angularFrequency / ShoreWaveNumber(angularFrequency, depth);
}

/// @brief 場のテクセルが波の出どころか（深い所と、範囲の上流側の辺の水）
/// @param direction うねりの進む向き（XZ・正規化済み）
bool IsShoreWaveSource(int2 coord, uint resolution, float depth, float startDepth, float2 direction)
{
    if (depth >= startDepth)
    {
        return true;
    }
    if (depth <= 0.0f)
    {
        return false;
    }
    const int last = (int)resolution - 1;
    return (coord.x == 0 && direction.x > kShoreWaveUpwindEdge)
        || (coord.x == last && -direction.x > kShoreWaveUpwindEdge)
        || (coord.y == 0 && direction.y > kShoreWaveUpwindEdge)
        || (coord.y == last && -direction.y > kShoreWaveUpwindEdge);
}

/// @brief 出どころでの到着時刻（沖を進む平面波の峰が位置 worldXZ に来る時刻）[s]
float ShoreWaveSourceArrival(float2 worldXZ, float2 direction, float angularFrequency, float startDepth)
{
    return dot(worldXZ, direction) / ShoreWavePhaseSpeed(angularFrequency, startDepth);
}

/// @brief 群速度（波のエネルギーが進む速さ）[m/s]
float ShoreWaveGroupSpeed(float angularFrequency, float waveNumber, float depth)
{
    const float kh = min(waveNumber * max(depth, kShoreWaveMinDepth), 20.0f);
    const float ratio = 0.5f * (1.0f + 2.0f * kh / sinh(2.0f * kh));
    return ratio * angularFrequency / waveNumber;
}

/// @brief 岸の波の設定（WaterFrameConstants・WaterSurfaceData のどちらからも組み立てる）
struct ShoreWaveParams
{
    bool enabled;
    float2 originXZ;       ///< 場の範囲の XZ の最小の角 [m]
    float size;            ///< 場の範囲の一辺の長さ [m]
    float edgeMargin;      ///< 範囲の端から内側へ、波を 0 から満量へ移す幅 [m]
    float angularFrequency;///< ω [rad/s]
    float height;          ///< 沖（水深 startDepth）での波高 [m]
    float startDepth;      ///< 波が来始める水深 [m]
    float heightVariation; ///< 1 本ごとの波高のばらつき（0 = 全部同じ）
};

/// @brief 岸の波の評価結果
struct ShoreWaveResult
{
    float3 displacement;   ///< 変位前の位置からのずれ [m]
    float2 normalSlope;    ///< 面の法線の n.xz / n.y（FFT の傾きと足し合わせる）
    float height;          ///< 今の位置の波高 [m]
    float breaking;        ///< 砕けている度合い [0,1]
    float fftScale;        ///< FFT の波に掛ける倍率（浅い所ほど小さい）
    float depth;           ///< ならした静水深 [m]
    float crest;           ///< 峰に近いほど 1（デバッグ表示）
};

/// @brief 場を双線形で読む（テクスチャのサンプラを使わない）
float4 SampleShoreWaveField(Texture2D<float4> field, float2 worldXZ, float2 originXZ, float size)
{
    uint width;
    uint heightTexels;
    field.GetDimensions(width, heightTexels);
    const float2 coord = (worldXZ - originXZ) / size * (float)width - 0.5f;
    const float2 base = floor(coord);
    const float2 f = coord - base;
    const int maxIndex = (int)width - 1;
    const int2 p00 = clamp((int2)base, 0, maxIndex);
    const int2 p11 = clamp((int2)base + 1, 0, maxIndex);
    const float4 c00 = field.Load(int3(p00.x, p00.y, 0));
    const float4 c10 = field.Load(int3(p11.x, p00.y, 0));
    const float4 c01 = field.Load(int3(p00.x, p11.y, 0));
    const float4 c11 = field.Load(int3(p11.x, p11.y, 0));
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

/// @brief 砕けた後の段差（ボア）の形に使う倍音の数
static const int kShoreWaveBoreHarmonics = 6;

/// @brief 砕けた後の段差の形（峰で急に上がり、次の峰までまっすぐ下がるのこぎり波）と、その位相での傾き
/// @details のこぎり波のフーリエ級数を kShoreWaveBoreHarmonics 項で打ち切り、Lanczos の係数で振動を抑える。
///          値は −1〜1 で平均 0。phi = 0 が峰
float2 EvaluateShoreWaveBore(float phi)
{
    float value = 0.0f;
    float rate = 0.0f;
    [unroll]
    for (int n = 1; n <= kShoreWaveBoreHarmonics; ++n)
    {
        const float x = kShoreWavePi * (float)n / (float)(kShoreWaveBoreHarmonics + 1);
        const float sigma = sin(x) / x;
        value += sigma * sin((float)n * phi) / (float)n;
        rate += sigma * cos((float)n * phi);
    }
    return float2(value, rate) * (2.0f / kShoreWavePi);
}

/// @brief 波の番号ごとの 0〜1 の値
float ShoreWaveHash(float waveIndex)
{
    uint x = (uint)(int)waveIndex * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return (float)(x & 0xFFFFu) / 65535.0f;
}

/// @brief 変位前の位置 baseXZ・時刻 phaseTime の岸の波を求める
/// @details 沖での波高 H0 に、浅水変形の倍率（群速度の比の平方根）と 1 本ごとのばらつきを掛け、
///          水深の 0.78 倍でなだらかに頭打ちさせる。頭打ちの手前から砕けているとみなす。
///          形は峰が尖り（進む向きの前後の動き）、浅いほど前の面が急になる（位相のゆがみ）
ShoreWaveResult EvaluateShoreWave(Texture2D<float4> field, ShoreWaveParams p, float2 baseXZ, float phaseTime)
{
    ShoreWaveResult result;
    result.displacement = float3(0.0f, 0.0f, 0.0f);
    result.normalSlope = float2(0.0f, 0.0f);
    result.height = 0.0f;
    result.breaking = 0.0f;
    result.fftScale = 1.0f;
    result.depth = p.startDepth;
    result.crest = 0.0f;
    if (!p.enabled || !IsInsideShoreMesh(baseXZ, p.originXZ, p.size))
    {
        return result;
    }
    const float edge = ComputeShoreMeshBlend(baseXZ, p.originXZ, p.size, p.edgeMargin);
    if (edge <= 0.0f)
    {
        return result;
    }

    const float4 sampled = SampleShoreWaveField(field, baseXZ, p.originXZ, p.size);
    const float arrival = sampled.x;
    const float2 arrivalGradient = sampled.yz;
    const float depth = sampled.w;
    result.depth = depth;

    // 浅い所ほど FFT の波を弱める（岸の波が受け持つ）
    result.fftScale = lerp(1.0f, smoothstep(0.2f * p.startDepth, p.startDepth, depth), edge);
    if (depth <= 0.0f)
    {
        return result;
    }

    const float omega = p.angularFrequency;
    const float waveNumber = ShoreWaveNumber(omega, depth);
    const float startWaveNumber = ShoreWaveNumber(omega, p.startDepth);
    const float shoaling = sqrt(
        ShoreWaveGroupSpeed(omega, startWaveNumber, p.startDepth) / ShoreWaveGroupSpeed(omega, waveNumber, depth));
    // 来始める水深から浅くなるにつれて出す（深い所は FFT の波が受け持つ）
    const float fadeIn = 1.0f - smoothstep(0.6f * p.startDepth, p.startDepth, depth);

    // 位相。峰の番号は峰に沿って同じなので、1 本の波は同じ高さのまま進む
    const float cycles = omega * (phaseTime - arrival) / (2.0f * kShoreWavePi);
    const float waveIndex = floor(cycles);
    const float within = cycles - waveIndex;
    const float variation = lerp(
        ShoreWaveHash(waveIndex), ShoreWaveHash(waveIndex + 1.0f), smoothstep(0.35f, 0.65f, within));
    const float heightScale = 1.0f + p.heightVariation * (2.0f * variation - 1.0f);

    // 波高。水深の kShoreWaveBreakingIndex 倍でなだらかに頭打ちさせる
    const float rawHeight = p.height * heightScale * shoaling * fadeIn * edge;
    const float ratio = rawHeight / max(kShoreWaveBreakingIndex * depth, 1.0e-4f);
    const float height = rawHeight * pow(1.0f + ratio * ratio * ratio * ratio, -0.25f);
    result.height = height;
    result.breaking = saturate((ratio - 0.85f) / 0.4f);

    // 形。前の面の急さ（位相のゆがみ）は浅くなるほど・砕けるほど強くする
    const float steepness = saturate(height / max(depth, kShoreWaveMinDepth) / kShoreWaveBreakingIndex);
    const float skew = lerp(0.45f * steepness, 0.85f, result.breaking);
    const float amplitude = 0.5f * height;
    const float phi = 2.0f * kShoreWavePi * within;
    const float warped = phi + skew * (cos(phi) - 1.0f);
    const float warpedRate = 1.0f - skew * sin(phi);

    // 進む向きの前後の動き（峰を尖らせる）。折り返さない大きさに抑える
    const float gradientLength = length(arrivalGradient);
    const float2 direction = gradientLength > 1.0e-6f ? arrivalGradient / gradientLength : float2(0.0f, 0.0f);
    const float sharpness = min(
        1.0f / tanh(max(waveNumber * depth, 1.0e-3f)),
        0.8f / max(waveNumber * amplitude * (1.0f + skew), 1.0e-4f));

    // 高さの形は、砕けるほど段差（前の面が切り立ち、後ろがまっすぐ下がる形）へ移す。
    // 前後の動きは段差では弱める
    const float2 bore = EvaluateShoreWaveBore(phi);
    const float shape = lerp(cos(warped), bore.x, result.breaking);
    const float shapeRate = lerp(-sin(warped) * warpedRate, bore.y, result.breaking);
    const float swing = 1.0f - 0.7f * result.breaking;

    const float eta = amplitude * shape;
    const float xi = sharpness * amplitude * sin(warped) * swing;
    result.displacement = float3(direction.x * xi, eta, direction.y * xi);
    result.crest = saturate(shape) * edge;

    // 法線。変位前の位置 x0 での接線 ∂p/∂x, ∂p/∂z（p = x0 + 変位）の外積
    const float2 phaseGradient = -omega * arrivalGradient;
    const float2 etaGradient = amplitude * shapeRate * phaseGradient;
    const float xiRate = sharpness * amplitude * cos(warped) * warpedRate * swing;
    const float3 tangentX = float3(
        1.0f + direction.x * xiRate * phaseGradient.x, etaGradient.x, direction.y * xiRate * phaseGradient.x);
    const float3 tangentZ = float3(
        direction.x * xiRate * phaseGradient.y, etaGradient.y, 1.0f + direction.y * xiRate * phaseGradient.y);
    const float3 normal = cross(tangentZ, tangentX);
    result.normalSlope = normal.xz / max(normal.y, 1.0e-3f);
    return result;
}

#endif // WATER_SHORE_WAVE_INCLUDED

// ============================================================
// レイトレーシングのデバッグ表示：中間結果に色を付ける
// 1 枚のテクスチャの 1 成分（または RGB）を、指定した範囲と色の付け方で表示用の色にする。
// あわせて、見ている成分の NaN・Inf の画素数と有限の値の最小・最大、指定した画素の生の値を数える。
// ============================================================

cbuffer RTDebugVisualizeConstants : register(b0)
{
    uint gWidth;          // 元のテクスチャの幅
    uint gHeight;         // 元のテクスチャの高さ
    uint gMode;           // 色の付け方（RTDebugViewMode）
    uint gComponent;      // 1 成分の色の付け方で見る成分（0〜3）
    float gRangeMin;      // 範囲の下限
    float gRangeMax;      // 範囲の上限
    uint gComponentMask;  // 色そのままのときに出す成分（RGBA のビット）
    uint gWriteOutput;    // 1 なら表示用の色を書く（0 なら数えるだけ）
    uint gProbeX;         // 生の値を読む画素
    uint gProbeY;
    uint gProbeEnabled;   // 1 なら gProbeX, gProbeY の値を書く
    uint gStatsOffset;    // 数えた値を書くバイト位置
    uint gAlphaMarksMiss; // 1 なら A = 0 の画素を「当たっていない」として数えずに模様で出す
    uint gPad0;
    uint gPad1;
    uint gPad2;
};

Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);
RWByteAddressBuffer gDebugData : register(u1);

#define kModeMono 0
#define kModeFalseColor 1
#define kModeHeat 2
#define kModeSigned 3
#define kModeNormal 4
#define kModeColor 5
#define kModeId 6
#define kModeBits 7

// 数えた値の並び（C++ 側 RTDebugSystem の kStats* と同じ）
static const uint kStatsNan = 0;
static const uint kStatsInf = 4;
static const uint kStatsFinite = 8;
static const uint kStatsMin = 12;
static const uint kStatsMax = 16;
static const uint kStatsProbe = 20;

/// @brief float の大小と同じ順に並ぶ uint へ直す
uint FloatToOrderedKey(float value)
{
    const uint bits = asuint(value);
    return ((bits & 0x80000000u) != 0) ? ~bits : (bits | 0x80000000u);
}

/// @brief sRGB の値を表示先（リニア）の値へ直す
float3 SrgbToLinear(float3 c)
{
    c = saturate(c);
    return select(c <= 0.04045f, c / 12.92f, pow((c + 0.055f) / 1.055f, 2.4f));
}

/// @brief 範囲の下限を 0・上限を 1 にした値
float NormalizeRange(float value)
{
    const float span = gRangeMax - gRangeMin;
    return (abs(span) > 1.0e-20f) ? (value - gRangeMin) / span : (value >= gRangeMin ? 1.0f : 0.0f);
}

/// @brief 疑似カラー（Turbo の近似。0 = 青・1 = 赤）
float3 FalseColor(float t)
{
    t = saturate(t);
    const float4 kr = float4(0.13572138f, 4.61539260f, -42.66032258f, 132.13108234f);
    const float2 kr2 = float2(-152.94239396f, 59.28637943f);
    const float4 kg = float4(0.09140261f, 2.19418839f, 4.84296658f, -14.18503333f);
    const float2 kg2 = float2(4.27729857f, 2.82956604f);
    const float4 kb = float4(0.10667330f, 12.64194608f, -60.58204836f, 110.36276771f);
    const float2 kb2 = float2(-89.90310912f, 27.34824973f);
    const float4 v4 = float4(1.0f, t, t * t, t * t * t);
    const float2 v2 = v4.zw * v4.z;
    return float3(dot(v4, kr) + dot(v2, kr2), dot(v4, kg) + dot(v2, kg2), dot(v4, kb) + dot(v2, kb2));
}

/// @brief ヒートマップ（0 以下は黒・緑 → 黄 → 赤）
float3 HeatColor(float value)
{
    if (!(value > 0.0f))
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float t = saturate(NormalizeRange(value));
    return (t < 0.5f) ? lerp(float3(0.0f, 0.6f, 0.1f), float3(1.0f, 0.9f, 0.0f), t * 2.0f)
                      : lerp(float3(1.0f, 0.9f, 0.0f), float3(1.0f, 0.05f, 0.0f), t * 2.0f - 1.0f);
}

/// @brief 番号ごとの色
float3 IdColor(uint id)
{
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return float3((h & 0xFFu), ((h >> 8u) & 0xFFu), ((h >> 16u) & 0xFFu)) / 255.0f * 0.8f + 0.2f;
}

/// @brief ビットの帯の色
float3 BitColor(uint bit)
{
    const float3 kColors[8] = {
        float3(1.0f, 0.25f, 0.2f), float3(0.25f, 0.85f, 0.3f), float3(0.3f, 0.5f, 1.0f), float3(1.0f, 0.85f, 0.2f),
        float3(0.9f, 0.35f, 0.9f), float3(0.25f, 0.9f, 0.9f), float3(1.0f, 0.6f, 0.25f), float3(0.95f, 0.95f, 0.95f),
    };
    return kColors[bit & 7u];
}

/// @brief 見る成分か（1 成分の色の付け方は gComponent だけ・法線は RGB・色そのままは gComponentMask）
bool IsConsidered(uint c)
{
    if (gMode == kModeNormal)
    {
        return c < 3u;
    }
    if (gMode == kModeColor)
    {
        return (gComponentMask & (1u << c)) != 0;
    }
    return c == gComponent;
}

/// @brief 表示用の色（sRGB の値）
float3 Colorize(float4 value, uint2 pixel)
{
    const float s = value[min(gComponent, 3u)];
    if (gMode != kModeNormal && gMode != kModeColor)
    {
        if (isnan(s))
        {
            return float3(1.0f, 0.0f, 1.0f);
        }
        if (isinf(s))
        {
            return (s > 0.0f) ? float3(1.0f, 1.0f, 1.0f) : float3(0.0f, 1.0f, 1.0f);
        }
    }

    switch (gMode)
    {
    case kModeFalseColor:
        return FalseColor(NormalizeRange(s));
    case kModeHeat:
        return HeatColor(s);
    case kModeSigned:
    {
        const float magnitude = abs(s);
        if (magnitude <= gRangeMin)
        {
            return float3(0.0f, 0.0f, 0.0f);
        }
        const float t = 0.2f + 0.8f * saturate(NormalizeRange(magnitude));
        return (s < 0.0f) ? float3(0.15f, 0.4f, 1.0f) * t : float3(1.0f, 0.3f, 0.1f) * t;
    }
    case kModeNormal:
        return any(isnan(value.xyz)) ? float3(1.0f, 0.0f, 1.0f) : saturate(value.xyz * 0.5f + 0.5f);
    case kModeColor:
    {
        float4 normalized = float4(NormalizeRange(value.x), NormalizeRange(value.y),
            NormalizeRange(value.z), NormalizeRange(value.w));
        if (any(isnan(normalized)))
        {
            return float3(1.0f, 0.0f, 1.0f);
        }
        // 成分を 1 つだけ選んだときは白黒で出す
        const uint mask = gComponentMask & 0xFu;
        if (countbits(mask) == 1u)
        {
            return saturate(normalized[firstbitlow(mask)]).xxx;
        }
        return saturate(float3(
            (mask & 1u) ? normalized.x : 0.0f,
            (mask & 2u) ? normalized.y : 0.0f,
            (mask & 4u) ? normalized.z : 0.0f));
    }
    case kModeId:
        return (s < 0.0f) ? float3(0.0f, 0.0f, 0.0f) : IdColor(uint(s + 0.5f));
    case kModeBits:
    {
        const uint bits = (s < 0.0f) ? 0u : uint(s + 0.5f);
        const uint band = (pixel.x >> 1u) & 7u;
        return ((bits >> band) & 1u) ? BitColor(band) : float3(0.06f, 0.06f, 0.06f);
    }
    default:
        return saturate(NormalizeRange(s)).xxx;
    }
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const bool inside = all(pixel < uint2(gWidth, gHeight));
    const float4 value = inside ? gSource.Load(int3(pixel, 0)) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    const bool miss = (gAlphaMarksMiss != 0) && !(value.w != 0.0f);

    // 見ている成分の NaN・Inf と有限の値の最小・最大
    uint nanPixel = 0;
    uint infPixel = 0;
    uint minKey = 0xFFFFFFFFu;
    uint maxKey = 0u;
    if (inside && !miss)
    {
        [unroll]
        for (uint c = 0; c < 4u; ++c)
        {
            if (!IsConsidered(c))
            {
                continue;
            }
            const float x = value[c];
            if (isnan(x))
            {
                nanPixel = 1;
            }
            else if (isinf(x))
            {
                infPixel = 1;
            }
            else
            {
                const uint key = FloatToOrderedKey(x);
                minKey = min(minKey, key);
                maxKey = max(maxKey, key);
            }
        }
        if (nanPixel != 0)
        {
            infPixel = 0;
        }
    }
    const uint finitePixel = (inside && !miss && nanPixel == 0 && infPixel == 0) ? 1u : 0u;

    const uint nanSum = WaveActiveSum(nanPixel);
    const uint infSum = WaveActiveSum(infPixel);
    const uint finiteSum = WaveActiveSum(finitePixel);
    const uint waveMin = WaveActiveMin(minKey);
    const uint waveMax = WaveActiveMax(maxKey);
    if (WaveIsFirstLane())
    {
        uint previous;
        if (nanSum != 0)
        {
            gDebugData.InterlockedAdd(gStatsOffset + kStatsNan, nanSum, previous);
        }
        if (infSum != 0)
        {
            gDebugData.InterlockedAdd(gStatsOffset + kStatsInf, infSum, previous);
        }
        if (finiteSum != 0)
        {
            gDebugData.InterlockedAdd(gStatsOffset + kStatsFinite, finiteSum, previous);
        }
        if (waveMin != 0xFFFFFFFFu)
        {
            gDebugData.InterlockedMin(gStatsOffset + kStatsMin, waveMin, previous);
        }
        if (waveMax != 0u)
        {
            gDebugData.InterlockedMax(gStatsOffset + kStatsMax, waveMax, previous);
        }
    }

    if (!inside)
    {
        return;
    }
    if (gProbeEnabled != 0 && pixel.x == gProbeX && pixel.y == gProbeY)
    {
        gDebugData.Store4(gStatsOffset + kStatsProbe, asuint(value));
    }
    if (gWriteOutput != 0)
    {
        float3 color;
        if (miss)
        {
            // 当たっていない画素は暗い市松模様
            color = (((pixel.x >> 3u) ^ (pixel.y >> 3u)) & 1u) ? float3(0.10f, 0.10f, 0.12f) : float3(0.16f, 0.16f, 0.18f);
        }
        else
        {
            color = Colorize(value, pixel);
        }
        gOutput[pixel] = float4(SrgbToLinear(color), 1.0f);
    }
}

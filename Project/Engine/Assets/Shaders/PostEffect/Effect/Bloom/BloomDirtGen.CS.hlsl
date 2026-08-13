// BloomDirtGen.CS.hlsl - レンズダートマスクの手続き生成（起動時に 1 回だけ実行）
//
// 実カメラのレンズには微細な塵・皮脂・拭き跡があり、強い光が入ると散乱して光る。
// テクスチャアセットを持ち込まずに済むよう、ソフトな円形の塵と細長い拭き跡を
// 固定シードのハッシュでばら撒いて生成する。周辺ほど濃くする（レンズ端は汚れやすい）。

RWTexture2D<float> gOutput : register(u0);

cbuffer DirtGenParams : register(b0)
{
    uint textureSize;
    float3 pad;
};

/// @brief 固定シードの 1D ハッシュ → [0,1]
float Hash(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / float(0x7fffffff);
}

static const uint kGroupSize = 8;
static const uint kDustCount = 96;    // 塵（ソフトな円）
static const uint kStreakCount = 14;  // 拭き跡（細長い楕円）

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= textureSize || dispatchId.y >= textureSize)
    {
        return;
    }

    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float(textureSize);
    float result = 0.0f;

    // ---- 塵: 大小のソフトな円をガウス減衰で重ねる ----
    for (uint i = 0; i < kDustCount; ++i)
    {
        const float2 center = float2(Hash(i * 4u + 1u), Hash(i * 4u + 2u));
        const float radius = lerp(0.003f, 0.035f, Hash(i * 4u + 3u) * Hash(i * 4u + 3u));
        const float strength = lerp(0.15f, 1.0f, Hash(i * 4u + 4u));
        const float d = length(uv - center);
        result += strength * exp(-(d * d) / (2.0f * radius * radius));
    }

    // ---- 拭き跡: 方向を持った細長い楕円 ----
    for (uint s = 0; s < kStreakCount; ++s)
    {
        const uint base = 1000u + s * 5u;
        const float2 center = float2(Hash(base + 1u), Hash(base + 2u));
        const float angle = Hash(base + 3u) * 6.2831853f;
        const float2 dir = float2(cos(angle), sin(angle));
        const float2 rel = uv - center;
        const float along = dot(rel, dir);
        const float across = dot(rel, float2(-dir.y, dir.x));
        const float lenR = lerp(0.05f, 0.22f, Hash(base + 4u)); // 長軸
        const float widR = lerp(0.002f, 0.006f, Hash(base + 5u)); // 短軸
        const float e = (along * along) / (lenR * lenR) + (across * across) / (widR * widR);
        result += 0.35f * exp(-e);
    }

    // ---- 周辺減光の逆: レンズ端ほど汚れが目立つ ----
    const float edge = length(uv - 0.5f) * 1.4142f; // 中心0 → 隅1
    result *= lerp(0.35f, 1.0f, edge * edge);

    gOutput[dispatchId.xy] = saturate(result);
}

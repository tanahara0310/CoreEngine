// ============================================================
// 岸の波のしぶき（粒の形・乱数・描画の共通定数）
// ------------------------------------------------------------
// 粒の並びは C++ の WaterSpraySystem::Particle と同じ（64 バイト）
// ============================================================
#ifndef WATER_SPRAY_INCLUDED
#define WATER_SPRAY_INCLUDED

/// @brief しぶき（流体描画では水の塊になる粒）
static const uint kSprayKindDroplet = 0u;
/// @brief 霧（細かいしぶき。ぼかした板で描く）
static const uint kSprayKindMist = 1u;

static const float kSprayPi = 3.14159265f;

// 描く粒の引数の並び（C++ の kDrawArgs* と同じ）
static const uint kSprayArgsDroplet = 0u;      ///< しぶきの DRAW 引数（頂点数・インスタンス数・最初の頂点・最初のインスタンス）
static const uint kSprayArgsMist = 4u;         ///< 霧の DRAW 引数
static const uint kSprayArgsDropletCount = 8u; ///< 生きているしぶきの数（64 ビットの下位）

/// @brief 霧の板が寿命の終わりまでに広がる倍率
static const float kSprayMistGrowth = 1.5f;

struct WaterSprayParticle
{
    float3 position;     ///< ワールド位置 [m]
    float age;           ///< 生まれてからの時間 [s]
    float3 velocity;     ///< 速度 [m/s]
    float lifetime;      ///< 寿命 [s]（0 = 空き）
    float3 prevPosition; ///< 前のフレームの位置（モーションベクター用）
    float radius;        ///< 半径 [m]
    uint kind;           ///< kSprayKindDroplet / kSprayKindMist
    float seed;          ///< 粒ごとの 0〜1 の値
    float2 pad;
};

/// @brief 生きている粒か
bool IsSprayAlive(WaterSprayParticle p)
{
    return p.lifetime > 0.0f && p.age < p.lifetime;
}

/// @brief 整数のハッシュ（PCG）
uint SprayHash(uint x)
{
    uint state = x * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// @brief 0〜1 の乱数（状態を進める）
float SprayRandom(inout uint state)
{
    state = SprayHash(state);
    return (float)(state & 0x00FFFFFFu) / 16777215.0f;
}

/// @brief 描くときの半径 [m]（霧は寿命につれて広がる）
float ComputeSprayDrawRadius(WaterSprayParticle p)
{
    if (p.kind == kSprayKindMist)
    {
        return p.radius * (1.0f + kSprayMistGrowth * saturate(p.age / max(p.lifetime, 1.0e-3f)));
    }
    return p.radius;
}

/// @brief 生まれた直後と寿命の終わりで薄くする倍率
float ComputeSprayLifeFade(WaterSprayParticle p)
{
    const float fadeIn = saturate(p.age / 0.06f);
    const float fadeOut = saturate((p.lifetime - p.age) / (p.kind == kSprayKindMist ? 0.8f : 0.25f));
    return fadeIn * fadeOut;
}

/// @brief 1 枚の板（2 枚の三角形）の 6 頂点の角 [-1,1]²
static const float2 kSprayQuadCorners[6] =
{
    float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, -1.0f),
    float2(1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
};

#endif // WATER_SPRAY_INCLUDED

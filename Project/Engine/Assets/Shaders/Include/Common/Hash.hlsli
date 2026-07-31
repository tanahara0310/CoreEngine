/// @file Hash.hlsli
/// @brief 汎用ハッシュ（擬似乱数）の共通実装
/// @details 系統が違うと結果も変わるため、名前で系統を区別している。
///          - Hash1x / Hash3x : 三角関数を使わない frac ベース（Dave Hoskins "Hash without Sine"）。
///                              GPU 間で結果が安定するのでノイズ生成やディザに向く。
///          - HashSine1x      : sin(dot(...)) ベースの古典的な擬似乱数。
///                              精度が実装依存だが既存の見た目を維持するために残している。
///          雲のノイズが使う整数格子ハッシュ（PCG）は用途が特殊なため
///          Cloud/Common/CloudNoiseCommon.hlsli 側に置いたまま。

#ifndef HASH_HLSLI
#define HASH_HLSLI

// ===================================================================
// frac ベース（三角関数なし）
// ===================================================================
/// @brief float2 → [0,1) のスカラー
float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

/// @brief float2 → [0,1)^3
float3 Hash32(float2 p)
{
    return float3(
        Hash12(p + float2(1.0f, 0.0f)),
        Hash12(p + float2(0.0f, 1.0f)),
        Hash12(p + float2(1.0f, 1.0f)));
}

// ===================================================================
// sin ベース（古典的）
// ===================================================================
/// @brief float → [0,1) のスカラー
float HashSine11(float n)
{
    return frac(sin(n * 127.1f + 311.7f) * 43758.5453f);
}

/// @brief float2 → [0,1) のスカラー
float HashSine12(float2 p)
{
    return frac(sin(dot(p.xy, float2(12.9898f, 78.233f))) * 43758.5453123f);
}

#endif // HASH_HLSLI

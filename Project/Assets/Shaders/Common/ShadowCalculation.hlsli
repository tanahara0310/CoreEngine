/// @brief シャドウマップ関連の共通関数

// PCFカーネルサイズ設定（0: 3x3, 1: 5x5, 2: 7x7）
#ifndef PCF_KERNEL_SIZE
#define PCF_KERNEL_SIZE 1  // デフォルト: 5x5
#endif

// シャドウマップの解像度
#ifndef SHADOW_MAP_SIZE
#define SHADOW_MAP_SIZE 4096.0f
#endif

/// @brief シャドウ計算（PCFあり・RasterizerStateバイアス使用）
/// @param lightSpacePos ライト空間での座標
/// @param normal ワールド空間での法線ベクトル（現在未使用）
/// @param lightDir ライトの方向ベクトル（現在未使用）
/// @param shadowMap シャドウマップテクスチャ
/// @param shadowSampler シャドウマップ用比較サンプラー
/// @return シャドウファクター（0.0 = 完全に影、1.0 = 影なし）
float CalculateShadow(float4 lightSpacePos, float3 normal, float3 lightDir, Texture2D<float> shadowMap, SamplerComparisonState shadowSampler)
{
    // 透視除算
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    // NDC座標 [-1, 1] から UV座標 [0, 1] へ変換
    float2 uv = projCoords.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y; // Y軸を反転
    
    // シャドウマップの範囲外は影なし
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
    {
        return 1.0f;
    }
    
    // 深度範囲外も影なし
    if (projCoords.z < 0.0f || projCoords.z > 1.0f)
    {
        return 1.0f;
    }
    
    // バイアスはRasterizerStateで設定されるため、手動計算は不要
    float currentDepth = projCoords.z;
    
    // PCF (Percentage Closer Filtering)
    float shadow = 0.0f;
    float texelSize = 1.0f / SHADOW_MAP_SIZE;
    
#if PCF_KERNEL_SIZE == 0
    // 3x3 カーネル (9サンプル) - 最も高速
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, currentDepth);
        }
    }
    shadow /= 9.0f;
    
#elif PCF_KERNEL_SIZE == 1
    // 5x5 カーネル (25サンプル) - バランス型
    [unroll]
    for (int x = -2; x <= 2; ++x)
    {
        [unroll]
        for (int y = -2; y <= 2; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, currentDepth);
        }
    }
    shadow /= 25.0f;
    
#else
    // 7x7 カーネル (49サンプル) - 最も高品質だが重い
    [unroll]
    for (int x = -3; x <= 3; ++x)
    {
        [unroll]
        for (int y = -3; y <= 3; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, currentDepth);
        }
    }
    shadow /= 49.0f;
#endif
    
    return shadow;
}

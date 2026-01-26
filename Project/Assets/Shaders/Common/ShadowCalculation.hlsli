/// @brief シャドウマップ関連の共通関数

/// @brief シャドウ計算（PCFあり）
/// @param lightSpacePos ライト空間での座標
/// @param shadowMap シャドウマップテクスチャ
/// @param shadowSampler シャドウマップ用比較サンプラー
/// @return シャドウファクター（0.0 = 完全に影、1.0 = 影なし）
float CalculateShadow(float4 lightSpacePos, Texture2D<float> shadowMap, SamplerComparisonState shadowSampler)
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
    
    // バイアス（シャドウアクネ対策）
    float bias = 0.005f;
    float currentDepth = projCoords.z - bias;
    
    // PCF (Percentage Closer Filtering) - 3x3サンプリング
    float shadow = 0.0f;
    float texelSize = 1.0f / 2048.0f; // シャドウマップサイズ
    
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, currentDepth);
        }
    }
    
    shadow /= 9.0f; // 9サンプルの平均
    
    return shadow;
}

/// @brief シンプルなシャドウ計算（PCFなし、デバッグ用）
/// @param lightSpacePos ライト空間での座標
/// @param shadowMap シャドウマップテクスチャ
/// @param shadowSampler シャドウマップ用比較サンプラー
/// @return シャドウファクター（0.0 = 完全に影、1.0 = 影なし）
float CalculateShadowSimple(float4 lightSpacePos, Texture2D<float> shadowMap, SamplerComparisonState shadowSampler)
{
    // 透視除算
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    // NDC座標からUV座標へ変換
    float2 uv = projCoords.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    
    // 範囲外チェック
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
    {
        return 1.0f;
    }
    
    if (projCoords.z < 0.0f || projCoords.z > 1.0f)
    {
        return 1.0f;
    }
    
    // バイアス
    float bias = 0.005f;
    float currentDepth = projCoords.z - bias;
    
    // 単一サンプル
    return shadowMap.SampleCmpLevelZero(shadowSampler, uv, currentDepth);
}

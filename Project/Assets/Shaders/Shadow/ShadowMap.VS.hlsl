#include "ShadowMap.hlsli"

/// @brief シャドウマップ生成用頂点シェーダー（通常モデル用）
/// @note 深度のみを書き込むため、ピクセルシェーダーは不要

ConstantBuffer<LightTransform> gLightTransform : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
};

/// @brief メイン関数
/// @param input 頂点入力
/// @return ライト空間でのクリップ座標
float4 main(VertexShaderInput input) : SV_POSITION
{
    // ワールド座標へ変換
    float4 worldPos = mul(input.position, gLightTransform.World);
    
    // ライト空間のクリップ座標へ変換（World * LightViewProjection）
    float4 clipPos = mul(worldPos, gLightTransform.LightViewProjection);
    
    return clipPos;
}

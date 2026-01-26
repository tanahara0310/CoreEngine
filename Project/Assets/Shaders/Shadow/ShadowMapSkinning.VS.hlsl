#include "ShadowMap.hlsli"

/// @brief シャドウマップ生成用頂点シェーダー（スキニングモデル用）
/// @note 深度のみを書き込むため、ピクセルシェーダーは不要

ConstantBuffer<LightTransform> gLightTransform : register(b0);

/// @brief スキニング用のウェイト行列
struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

/// @brief MatrixPaletteをStructuredBufferとして受け取る
StructuredBuffer<Well> gMatrixPalette : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    int4 index : INDEX0;
};

/// @brief スキニング計算
/// @param input 頂点入力
/// @return スキニング後の位置
float4 CalculateSkinning(VertexShaderInput input)
{
    // 各ボーンの影響を加算
    float4 skinnedPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    skinnedPosition += mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    skinnedPosition += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    skinnedPosition += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    skinnedPosition += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
    
    skinnedPosition.w = 1.0f; // 確実にwを1にする
    
    return skinnedPosition;
}

/// @brief メイン関数
/// @param input 頂点入力
/// @return ライト空間でのクリップ座標
float4 main(VertexShaderInput input) : SV_POSITION
{
    // スキニング計算
    float4 skinnedPosition = CalculateSkinning(input);
    
    // ワールド座標へ変換
    float4 worldPos = mul(skinnedPosition, gLightTransform.World);
    
    // ライト空間のクリップ座標へ変換
    return mul(worldPos, gLightTransform.LightViewProjection);
}

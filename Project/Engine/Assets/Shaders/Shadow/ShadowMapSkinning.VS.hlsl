#include "ShadowMap.hlsli"

/// @brief シャドウマップ生成用頂点シェーダー（スキニングモデル用）
/// @note スキニング計算はComputeShader(Skinning.CS.hlsl)側で完了済み。
///       ここではCS出力済みの頂点(既にスキン後のローカル空間座標)をライト空間へ変換するだけでよい。
///       深度のみを書き込むため、ピクセルシェーダーは不要。

ConstantBuffer<LightTransform> gLightTransform : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

/// @brief メイン関数
/// @param input 頂点入力
/// @return ライト空間でのクリップ座標
float4 main(VertexShaderInput input) : SV_POSITION
{
    // ワールド座標へ変換
    float4 worldPos = mul(input.position, gLightTransform.World);

    // ライト空間のクリップ座標へ変換
    return mul(worldPos, gLightTransform.LightViewProjection);
}

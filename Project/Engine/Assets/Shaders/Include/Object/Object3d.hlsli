struct VertexShaderOutput
{
    float4 position : SV_POSITION; // クリップ空間位置
    float2 texcoord : TEXCOORD0; // テクスチャ座標
    float3 normal : NORMAL0; // 法線（ワールド空間）
    float3 worldPosition : POSITION0; // ワールド空間位置
    float4 lightSpacePos : POSITION1; // ライト空間位置（シャドウマッピング用）
    float3 tangent : TANGENT0; // 接線（ワールド空間）
    float3 bitangent : BINORMAL0; // 従法線（ワールド空間）
    float4 clipPosCurrent : POSITION2; // 現フレームのクリップ空間位置（モーションベクター用）
    float4 clipPosPrev : POSITION3; // 前フレームのクリップ空間位置（モーションベクター用）
};

struct TransformationMatrix
{
    float4x4 WVP; // ワールドビュー射影行列
    float4x4 World; // ワールド行列
    float4x4 WorldInversTranspose; // ワールド行列の逆行列の転置（法線変換用）
    float4x4 LightViewProjection; // ライトのビュー射影行列（シャドウマッピング用）
    float4x4 PrevWVP; // 前フレームのワールドビュー射影行列（モーションベクター用）
};

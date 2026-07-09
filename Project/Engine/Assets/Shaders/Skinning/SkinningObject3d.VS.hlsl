#include "../Object/Object3d.hlsli"

// スキニング計算はComputeShader(Skinning.CS.hlsl)側で完了済み。
// ここではCS出力済みの頂点(position/normal/tangentは既にスキン後のローカル空間座標)を
// 通常モデルと同じくワールド/WVP変換するだけでよい。

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.texcoord = input.texcoord;
    output.position = mul(input.position, gTransformationMatrix.WVP);

    // 法線をワールド空間に変換
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrix.WorldInversTranspose));

    // タンジェントをワールド空間に変換
    output.tangent = normalize(mul(input.tangent, (float3x3) gTransformationMatrix.World));

    // バイタンジェント（従接線）を計算（法線とタンジェントの外積）
    output.bitangent = normalize(cross(output.normal, output.tangent));

    // ワールド座標を計算
    float4 worldPos = mul(input.position, gTransformationMatrix.World);
    output.worldPosition = worldPos.xyz;

    // ライト空間座標を計算（シャドウマップ用）
    output.lightSpacePos = mul(worldPos, gTransformationMatrix.LightViewProjection);

    return output;
}

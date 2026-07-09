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
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrix.WorldInversTranspose));
    output.tangent = normalize(mul(input.tangent, (float3x3) gTransformationMatrix.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    float4 worldPos = mul(input.position, gTransformationMatrix.World);
    output.worldPosition = worldPos.xyz;
    output.lightSpacePos = mul(worldPos, gTransformationMatrix.LightViewProjection);

    // モーションベクター用: 現フレーム・前フレームのクリップ座標
    output.clipPosCurrent = mul(input.position, gTransformationMatrix.WVP);
    output.clipPosPrev = mul(input.position, gTransformationMatrix.PrevWVP);

    return output;
}

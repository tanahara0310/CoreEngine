#include "Object3d.hlsli"

// インスタンシング描画: 1 つの DrawIndexedInstanced で複数インスタンスを描画する
StructuredBuffer<TransformationMatrix> gInstanceData : register(t0, space1);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    VertexShaderOutput output;
    output.texcoord = input.texcoord;
    output.position = mul(input.position, mtx.WVP);

    output.normal = normalize(mul(input.normal, (float3x3) mtx.WorldInversTranspose));
    output.tangent = normalize(mul(input.tangent, (float3x3) mtx.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    float4 worldPos = mul(input.position, mtx.World);
    output.worldPosition = worldPos.xyz;
    output.lightSpacePos = mul(worldPos, mtx.LightViewProjection);

    // モーションベクター用: 現フレーム・前フレームのクリップ座標
    output.clipPosCurrent = mul(input.position, mtx.WVP);
    output.clipPosPrev = mul(input.position, mtx.PrevWVP);

    return output;
}

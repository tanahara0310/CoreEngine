#include "Object3d.hlsli"

// インスタンシング描画: 1 つの DrawIndexedInstanced で複数インスタンスを描画する
// Root SRV としてバインドされる StructuredBuffer<TransformationMatrix>
StructuredBuffer<TransformationMatrix> gInstanceData : register(t0, space1);


struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;  // タンジェント（接線）
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    VertexShaderOutput output;
    output.texcoord = input.texcoord;
    output.position = mul(input.position, mtx.WVP);

    // 法線をワールド空間に変換
    output.normal = normalize(mul(input.normal, (float3x3)mtx.WorldInversTranspose));

    // タンジェントをワールド空間に変換
    output.tangent = normalize(mul(input.tangent, (float3x3)mtx.World));

    // バイタンジェント（従接線）を計算（法線とタンジェントの外積）
    output.bitangent = normalize(cross(output.normal, output.tangent));

    // ワールド座標を計算
    float4 worldPos = mul(input.position, mtx.World);
    output.worldPosition = worldPos.xyz;

    // ライト空間座標を計算（シャドウマップ用）
    output.lightSpacePos = mul(worldPos, mtx.LightViewProjection);

    return output;
}

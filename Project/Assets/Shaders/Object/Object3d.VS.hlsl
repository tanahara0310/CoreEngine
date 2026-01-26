#include "Object3d.hlsli"
	
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);


struct VertexShaderInput
{
	float4 position : POSITION0;
	float2 texcoord : TEXCOORD0;
	float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
	VertexShaderOutput output;
	output.texcoord = input.texcoord;
	output.position = mul(input.position, gTransformationMatrix.WVP);
	output.normal = normalize(mul(input.normal, (float3x3)gTransformationMatrix.WorldInversTranspose));
	
	// ワールド座標を計算
	float4 worldPos = mul(input.position, gTransformationMatrix.World);
	output.worldPosition = worldPos.xyz;
	
	// ライト空間座標を計算（シャドウマップ用）
	output.lightSpacePos = mul(worldPos, gTransformationMatrix.LightViewProjection);
	
	return output;
}

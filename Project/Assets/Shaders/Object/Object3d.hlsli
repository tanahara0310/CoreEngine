struct VertexShaderOutput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
	float3 normal : NORMAL0;
	float3 worldPosition : POSITION0;
	float4 lightSpacePos : POSITION1; // ライト空間座標（シャドウマップ用）
	float3 tangent : TANGENT0;        // タンジェント（接線）- ノーマルマップ用
	float3 bitangent : BINORMAL0;     // バイタンジェント（従接線）- ノーマルマップ用
};

struct TransformationMatrix
{
	float4x4 WVP;
	float4x4 World;
	float4x4 WorldInversTranspose;
	float4x4 LightViewProjection;
};

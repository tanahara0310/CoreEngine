#include "UI.hlsli"

// UI 専用 VS
//   WVP は C++ 側でスクリーン座標系の正射影 + ローカル変換を合成済みの行列を渡す
//   入力頂点はスプライトと同じ単位クワッド（位置・UV・法線）
cbuffer TransformationMatrix : register(b1)
{
    float4x4 WVP;
    float4x4 World;
}

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal   : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    return output;
}

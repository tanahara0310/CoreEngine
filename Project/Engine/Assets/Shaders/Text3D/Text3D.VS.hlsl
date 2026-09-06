#include "Text3D.hlsli"

ConstantBuffer<Text3DBatch> gBatch : register(b0);

// MSDF テキスト専用 VS（3D ワールド空間）
//   位置は CPU 側でワールド座標まで変換済み。
//   テキストごとのワールド行列を定数バッファへ渡さないのは、
//   渡すとテキストの数だけドローコールを分ける必要が出るため。
//   ここでは共通のビュー射影を掛けるだけで済む。
struct VertexShaderInput
{
    float4 position     : POSITION0;
    float3 texcoord     : TEXCOORD0;
    float4 color        : COLOR0;
    float4 outlineColor : COLOR1;
    float2 style        : TEXCOORD1;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position     = mul(input.position, gBatch.viewProjection);
    output.texcoord     = input.texcoord;
    output.color        = input.color;
    output.outlineColor = input.outlineColor;
    output.style        = input.style;
    return output;
}

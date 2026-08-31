#include "MsdfText.hlsli"

ConstantBuffer<TextBatch> gBatch : register(b0);

// MSDF テキスト専用 VS
//   位置は CPU 側でスクリーン px まで変換済み。
//   テキストごとのワールド行列を定数バッファへ渡さないのは、
//   渡すとテキストの数だけドローコールを分ける必要が出るため。
//   ここでは共通の射影を掛けるだけで済む。
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
    output.position     = mul(input.position, gBatch.projection);
    output.texcoord     = input.texcoord;
    output.color        = input.color;
    output.outlineColor = input.outlineColor;
    output.style        = input.style;
    return output;
}

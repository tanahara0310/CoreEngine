#include "MsdfText.hlsli"
#include "ColorSpace.hlsli"

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
    // 指定色は sRGB。トーンマップ後の色（リニア）へ直して渡す
    output.color        = float4(SRGBToLinear(input.color.rgb), input.color.a);
    output.outlineColor = float4(SRGBToLinear(input.outlineColor.rgb), input.outlineColor.a);
    output.style        = input.style;
    return output;
}

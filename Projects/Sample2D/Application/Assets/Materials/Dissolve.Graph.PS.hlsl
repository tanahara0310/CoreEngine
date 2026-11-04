// Generated from Application/Assets/Materials/Dissolve.shadergraph. Changes are overwritten when the graph is saved.
// property dissolve Float range 0.0f 1.0f
// property edgeColor Color hdr
// property edgeWidth Float range 0.0f 0.3f
// property noiseScale Float
#include "SpriteMaterial.hlsli"
#include "ShaderGraphLibrary.hlsli"

struct GraphProperties
{
    float4 edgeColor;
    float dissolve;
    float edgeWidth;
    float noiseScale;
};
ConstantBuffer<GraphProperties> gMaterial : register(b0);

struct GraphInput
{
    float2 uv;
    float3 positionWS;
    float4 spriteColor;
    float4 vertexColor;
    float2 textureSize;
    float  time;
    float  unscaledTime;
    float  deltaTime;
};

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    GraphInput IN;
    IN.uv = SpriteUV(input.texcoord);
    IN.positionWS = input.worldPos;
    IN.spriteColor = SpriteDefaultColor(input);
    IN.vertexColor = SpriteColor();
    IN.textureSize = gSpriteDraw.textureSize;
    IN.time = gSpriteDraw.time;
    IN.unscaledTime = gSpriteDraw.unscaledTime;
    IN.deltaTime = gSpriteDraw.deltaTime;

    // 10 Sprite Color
    float4 n10_RGBA = IN.spriteColor;
    float3 n10_RGB = IN.spriteColor.rgb;
    float n10_A = IN.spriteColor.a;
    // 12 Property
    float4 n12_Out = gMaterial.edgeColor;
    // 4 Property
    float n4_Out = gMaterial.dissolve;
    // 2 Property
    float n2_Out = gMaterial.noiseScale;
    // 3 Simple Noise
    float n3_Out = SG_SimpleNoise(IN.uv, n2_Out);
    // 5 Step
    float n5_Out = step(n4_Out, n3_Out);
    // 6 Property
    float n6_Out = gMaterial.edgeWidth;
    // 16 Step
    float n16_Out = step(0.001f, n4_Out);
    // 17 Multiply
    float n17_Out = (n6_Out * n16_Out);
    // 7 Add
    float n7_Out = (n4_Out + n17_Out);
    // 8 Step
    float n8_Out = step(n7_Out, n3_Out);
    // 9 Subtract
    float n9_Out = (n5_Out - n8_Out);
    // 11 Multiply
    float n11_Out = (n9_Out * n10_A);
    // 13 Multiply
    float4 n13_Out = (n12_Out * ((float4)(n11_Out)));
    // 14 Add
    float4 n14_Out = (float4(n10_RGB, 1.0f) + n13_Out);
    // 15 Multiply
    float n15_Out = (n10_A * n5_Out);

    float3 baseColor = (n14_Out).xyz;
    float alpha = n15_Out;
    return float4(baseColor, alpha);
}

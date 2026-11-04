// Generated from Application/Assets/Materials/ShineGraph.shadergraph. Changes are overwritten when the graph is saved.
// property shineColor Color hdr
// property speed Float
// property sharpness Float
// property phase Float
#include "SpriteMaterial.hlsli"
#include "ShaderGraphLibrary.hlsli"

struct GraphProperties
{
    float4 shineColor;
    float speed;
    float sharpness;
    float phase;
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

    // 12 Sprite Color
    float4 n12_RGBA = IN.spriteColor;
    float3 n12_RGB = IN.spriteColor.rgb;
    float n12_A = IN.spriteColor.a;
    // 13 Property
    float4 n13_Out = gMaterial.shineColor;
    // 2 Time
    float n2_Time = IN.time;
    float n2_SineTime = sin(IN.time);
    float n2_CosineTime = cos(IN.time);
    float n2_DeltaTime = IN.deltaTime;
    float n2_UnscaledTime = IN.unscaledTime;
    // 3 Property
    float n3_Out = gMaterial.speed;
    // 4 Multiply
    float n4_Out = (n2_Time * n3_Out);
    // 5 Property
    float n5_Out = gMaterial.phase;
    // 6 Add
    float n6_Out = (n4_Out + n5_Out);
    // 7 Sine
    float n7_Out = sin(n6_Out);
    // 8 Saturate
    float n8_Out = saturate(n7_Out);
    // 9 Property
    float n9_Out = gMaterial.sharpness;
    // 10 Maximum
    float n10_Out = max(n9_Out, 1.0f);
    // 11 Power
    float n11_Out = pow(max(n8_Out, 0.0f), n10_Out);
    // 14 Multiply
    float4 n14_Out = (n13_Out * ((float4)(n11_Out)));
    // 15 Multiply
    float4 n15_Out = (n14_Out * ((float4)(n12_A)));
    // 16 Add
    float4 n16_Out = (float4(n12_RGB, 1.0f) + n15_Out);

    float3 baseColor = (n16_Out).xyz;
    float alpha = IN.spriteColor.a;
    return float4(baseColor, alpha);
}

#include "Object3dForward.hlsli"

struct FFTWaterPSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

VertexShaderOutput ToVertexShaderOutput(FFTWaterPSInput input)
{
    VertexShaderOutput output;
    output.position = input.position;
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.worldPosition = input.worldPosition;
    output.lightSpacePos = input.lightSpacePos;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.clipPosCurrent = input.clipPosCurrent;
    output.clipPosPrev = input.clipPosPrev;
    return output;
}

PixelShaderOutput main(FFTWaterPSInput input)
{
    VertexShaderOutput forwardInput = ToVertexShaderOutput(input);
    PixelShaderOutput output;

    forwardInput.normal = normalize(forwardInput.normal);
    float3 toEye = normalize(gCamera.worldPosition - forwardInput.worldPosition);
    float finalAlpha = gMaterial.color.a;

    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color;
        return output;
    }

    float metallic = gMaterial.metallic;
    float roughness = max(gMaterial.roughness, 0.01f);
    float ao = 1.0f; // 水面は AO マップを持たないため遮蔽なし固定
    float3 albedo = gMaterial.color.rgb;

    output.color.rgb = CalculateAllLighting(
        forwardInput,
        albedo,
        metallic,
        roughness,
        ao,
        toEye,
        float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.rgb += ApplyIBL(forwardInput, albedo, metallic, roughness, ao, toEye);
    output.color.a = finalAlpha;
    return output;
}

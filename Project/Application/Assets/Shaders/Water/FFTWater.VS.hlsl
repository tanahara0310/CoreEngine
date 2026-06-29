#include "Object3dVertex.hlsli"

Texture2D<float4> gFFTOceanDisplacement : register(t18);
Texture2D<float4> gFFTOceanNormal : register(t19);
SamplerState gLinearClamp : register(s2);

cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int gClipEnabled;
    int gReflectionEnabled;
    float gFresnelReflectanceScale;
    float gFresnelBaseReflectance;
    float gAbsorptionCoeff;
    int gDepthFadeEnabled;
    int gDepthFadeDebugEnabled;
    float gDepthFadeDebugScale;
    float3 gShallowColor;
    float gShallowColorPad;
    float3 gDeepColor;
    float gDeepColorPad;
    uint gDepthDebugViewMode;
    float3 gDebugPadding;
};

struct FFTWaterVSOutput
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
    float clipDist : SV_ClipDistance0;
};

FFTWaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    float4 worldPos4 = mul(input.position, mtx.World);
    float3 baseWorldPos = worldPos4.xyz;
    float2 sampleUV = input.texcoord;

    float3 displacement = gFFTOceanDisplacement.SampleLevel(gLinearClamp, sampleUV, 0.0f).xyz;
    const float maxVerticalDisplacement = 0.5f;
    const float maxHorizontalDisplacement = 0.25f;
    displacement.x = clamp(displacement.x, -maxHorizontalDisplacement, maxHorizontalDisplacement);
    displacement.y = clamp(displacement.y, -maxVerticalDisplacement, maxVerticalDisplacement);
    displacement.z = clamp(displacement.z, -maxHorizontalDisplacement, maxHorizontalDisplacement);

    float3 encodedNormal = gFFTOceanNormal.SampleLevel(gLinearClamp, sampleUV, 0.0f).xyz;
    float3 fftNormal = normalize(encodedNormal * 2.0f - 1.0f);

    float3 worldPos = baseWorldPos + displacement;

    float3 tangent = normalize(float3(1.0f, 0.0f, 0.0f));
    float3 binormal = normalize(cross(fftNormal, tangent));
    tangent = normalize(cross(binormal, fftNormal));

    FFTWaterVSOutput output;
    output.texcoord = input.texcoord;

    float4 baseClip = mul(input.position, mtx.WVP);
    float3x3 invWorld3 = transpose((float3x3)mtx.WorldInversTranspose);
    float3 offsetLS = mul(displacement, invWorld3);
    float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = baseClip + offsetClip;

    output.normal = normalize(mul(fftNormal, (float3x3)mtx.WorldInversTranspose));
    output.tangent = normalize(mul(tangent, (float3x3)mtx.World));
    output.bitangent = normalize(mul(binormal, (float3x3)mtx.World));
    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP);
    output.clipDist = gClipEnabled ? dot(float4(worldPos, 1.0f), gClipPlane) : 1.0f;
    return output;
}

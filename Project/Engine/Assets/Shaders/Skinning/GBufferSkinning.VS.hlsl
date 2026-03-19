#include "../Object/Object3d.hlsli"

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

StructuredBuffer<Well> gMatrixPalette : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
    float4 weight : WEIGHT0;
    int4 index : INDEX0;
};

struct Skinned
{
    float4 position;
    float3 normal;
    float3 tangent;
};

Skinned Skinning(VertexShaderInput input)
{
    Skinned skinned;

    skinned.position = mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    skinned.position += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    skinned.position += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    skinned.position += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
    skinned.position.w = 1.0f;

    skinned.normal = mul(input.normal, (float3x3)gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
    skinned.normal += mul(input.normal, (float3x3)gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
    skinned.normal += mul(input.normal, (float3x3)gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
    skinned.normal += mul(input.normal, (float3x3)gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;
    skinned.normal = normalize(skinned.normal);

    skinned.tangent = mul(input.tangent, (float3x3)gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    skinned.tangent += mul(input.tangent, (float3x3)gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    skinned.tangent += mul(input.tangent, (float3x3)gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    skinned.tangent += mul(input.tangent, (float3x3)gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
    skinned.tangent = normalize(skinned.tangent);

    return skinned;
}

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    Skinned skinned = Skinning(input);

    output.texcoord = input.texcoord;
    output.position = mul(skinned.position, gTransformationMatrix.WVP);
    output.normal = normalize(mul(skinned.normal, (float3x3)gTransformationMatrix.WorldInversTranspose));
    output.tangent = normalize(mul(skinned.tangent, (float3x3)gTransformationMatrix.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    float4 worldPos = mul(skinned.position, gTransformationMatrix.World);
    output.worldPosition = worldPos.xyz;
    output.lightSpacePos = mul(worldPos, gTransformationMatrix.LightViewProjection);

    return output;
}

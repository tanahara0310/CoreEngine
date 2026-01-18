#include "../Object/Object3d.hlsli"

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

// GPU側のWellの定義
struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

// MatrixPaletteをStructuredBufferとして受け取る
StructuredBuffer<Well> gMatrixPalette : register(t0);

// 入力項点の拡張
struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    int4 index : INDEX0;
};

// Skinningの結果を受け取るための構造体
struct Skinned
{
    float4 position;
    float3 normal;
};

// Skinningを行う関数。入力項点に加工を行ってSkinning後の頂点を返却する
Skinned Skinning(VertexShaderInput input)
{
    Skinned skinned;
    
    // 位置の変換
    skinned.position = mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    skinned.position += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    skinned.position += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    skinned.position += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
    skinned.position.w = 1.0f; // 確実にwを1にする
    
    // 法線の変換
    skinned.normal = mul(input.normal, (float3x3) gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
    skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
    skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
    skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;
    skinned.normal = normalize(skinned.normal); // 正規化して返してあげる
    
    return skinned;
}

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // Skinning計算を行う
    Skinned skinned = Skinning(input);
    
    // Skinning後の頂点を使用して変換
    output.texcoord = input.texcoord;
    output.position = mul(skinned.position, gTransformationMatrix.WVP);
    output.normal = normalize(mul(skinned.normal, (float3x3) gTransformationMatrix.WorldInversTranspose));
    output.worldPosition = mul(skinned.position, gTransformationMatrix.World).xyz;
    
    return output;
}

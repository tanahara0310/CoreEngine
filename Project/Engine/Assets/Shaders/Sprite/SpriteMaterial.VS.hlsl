#include "SpriteMaterial.hlsli"

// マテリアルの標準の頂点シェーダ（.mat で頂点シェーダを指定しなければこれを使う）
SpriteVertexOutput main(SpriteVertexInput input)
{
    SpriteVertexOutput output;
    output.position = mul(input.position, gSpriteTransform.WVP);
    output.texcoord = input.texcoord;
    output.worldPos = mul(input.position, gSpriteTransform.World).xyz;
    return output;
}

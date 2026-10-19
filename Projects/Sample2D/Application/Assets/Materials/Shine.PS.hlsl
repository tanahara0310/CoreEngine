#include "SpriteMaterial.hlsli"

// きらっと光るマテリアル（コインとゴールの旗）
struct Params
{
    float4 shineColor; // 光ったときに足す色
    float  speed;      // 光る間隔の速さ
    float  sharpness;  // 大きいほど光る時間が短い
    float  phase;      // 光るタイミングのずれ（スプライトごとに上書きする）
};
ConstantBuffer<Params> gMaterial : register(b0);

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float4 color = SpriteDefaultColor(input);

    // sin の山の近くだけ明るくする
    float wave = saturate(sin(gSpriteDraw.time * gMaterial.speed + gMaterial.phase));
    float shine = pow(wave, max(gMaterial.sharpness, 1.0f));
    color.rgb += gMaterial.shineColor.rgb * shine * color.a;
    return color;
}

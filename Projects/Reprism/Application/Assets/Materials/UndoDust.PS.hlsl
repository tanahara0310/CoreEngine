#include "SpriteMaterial.hlsli"

// Undo の表示（UI 画像）。dissolve を進めると、ブロックごとにネオン色に光ってから崩れて消える
struct Params
{
    float4 color;    // 掛ける色（a が濃さ）
    float  time;     // 経過時間
    float  dissolve; // 崩れる進み 0 → 1
};
ConstantBuffer<Params> gMaterial : register(b0);

float Hash21(float2 p)
{
    p = frac(p * float2(127.1f, 311.7f));
    p += dot(p, p.yx + 19.19f);
    return frac((p.x + p.y) * p.x);
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float4 texColor = gTexture.Sample(gSampler, input.texcoord);
    if (texColor.a < 0.01f)
        discard;

    // ブロックごとに崩れる順番を決める
    const float kBlockSize = 0.04f;
    float2 blockUV = floor(input.texcoord / kBlockSize);
    float blockHash = Hash21(blockUV);
    if (blockHash < gMaterial.dissolve)
        discard;

    // 崩れる直前のブロックほど強く光る
    const float kGlowRange = 0.15f;
    float edgeFactor = 1.0f - saturate((blockHash - gMaterial.dissolve) / kGlowRange);
    float glowIntensity = edgeFactor * edgeFactor;

    float hue = blockHash;
    float3 neonA = float3(1.0f, 0.2f, 0.8f);
    float3 neonB = float3(0.2f, 1.0f, 0.8f);
    float3 neonColor = lerp(neonA, neonB, hue) * 2.0f;
    float3 glow = glowIntensity * neonColor;

    float3 rgb = gMaterial.color.rgb * texColor.rgb + glow;
    float alpha = gMaterial.color.a * texColor.a;
    return float4(rgb, alpha);
}

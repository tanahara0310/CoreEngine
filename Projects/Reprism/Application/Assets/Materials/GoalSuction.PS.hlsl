#include "SpriteMaterial.hlsli"

// 自機とハットの絵。progress を進めるとワープホールへ吸い込まれるように UV が渦を巻く。
// UV はコマ割り済みの値のまま使い、端の 0.08 を薄くする
struct Params
{
    float4 color;    // 掛ける色
    float  time;     // 経過時間
    float  progress; // 吸い込みの進み 0 → 1
    float  swirl;    // 渦の強さ
    float  stretch;  // 引き伸ばしの強さ
    float  alpha;    // 濃さ
};
ConstantBuffer<Params> gMaterial : register(b0);

float2 Rot(float2 v, float a)
{
    float s = sin(a);
    float c = cos(a);
    return float2(v.x * c - v.y * s, v.x * s + v.y * c);
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 uv = input.texcoord;
    float2 local = uv - 0.5f;
    float radius = length(local);
    float2 dir = local / max(radius, 0.001f);

    float p = saturate(gMaterial.progress);
    float bend = smoothstep(0.0f, 0.65f, p);
    float swirl = (1.0f - radius * 1.6f) * gMaterial.swirl * bend;
    float2 bent = Rot(local, swirl);

    // 中心へ吸い寄せて円弧状に曲げる
    float pull = bend * (0.18f + gMaterial.stretch * 0.12f) * (1.0f - radius);
    float2 suctionUv = bent + dir * pull;

    // 接線の向きに揺らして外周を引きずる
    float2 tangent = float2(-dir.y, dir.x);
    suctionUv += tangent * sin(radius * 9.0f - gMaterial.time * 4.0f) * 0.025f * bend;

    float2 sampleUv = suctionUv + 0.5f;
    float2 edgeFade = smoothstep(float2(0.0f, 0.0f), float2(0.08f, 0.08f), sampleUv)
                    * (1.0f - smoothstep(float2(0.92f, 0.92f), float2(1.0f, 1.0f), sampleUv));
    float uvAlpha = edgeFade.x * edgeFade.y;

    float4 tex = gTexture.Sample(gSampler, sampleUv);
    tex.rgb *= gMaterial.color.rgb;
    tex.a *= gMaterial.color.a * gMaterial.alpha * uvAlpha;
    return tex;
}

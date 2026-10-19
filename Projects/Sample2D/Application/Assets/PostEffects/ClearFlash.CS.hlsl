#include "PostEffectMaterial.hlsli"

// ゴールしたときに画面の真ん中から光の輪を広げる（progress を 0 から 1 へ進める）
struct Params
{
    float4 flashColor; // 輪の色（a は濃さ）
    float  progress;   // 0 = 始まり ／ 1 = 終わり
    float  width;      // 輪の太さ（画面の高さに対する割合）
};
ConstantBuffer<Params> gMaterial : register(b0);

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }
    float4 color = LoadScreen(id.xy);

    // 画面の高さを 1 とした、真ん中からの距離
    float2 uv = PostEffectUV(id.xy);
    float aspect = float(gPostEffect.screenSize.x) * gPostEffect.invScreenSize.y;
    float distanceFromCenter = length((uv - 0.5f) * float2(aspect, 1.0f));

    float radius = gMaterial.progress * 1.2f;
    float ring = 1.0f - smoothstep(0.0f, max(gMaterial.width, 0.001f), abs(distanceFromCenter - radius));
    float fade = 1.0f - saturate(gMaterial.progress);
    color.rgb = lerp(color.rgb, gMaterial.flashColor.rgb, saturate(ring * fade * gMaterial.flashColor.a));

    gOutput[id.xy] = color;
}

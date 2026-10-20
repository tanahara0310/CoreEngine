#include "PostEffectMaterial.hlsli"

// 画面の左上と右下の角から、縁に沿って線を伸ばす（スクリプトが progress と色の a を進める）
struct Params
{
    float4 invasionColor; // rgb = 線の色 ／ a = 濃さ
    float  progress;      // 線の伸び 0 = なし ／ 1 = いっぱい
    float  lineThickness; // 線の太さ（画面の UV）
};
ConstantBuffer<Params> gMaterial : register(b0);

// 1 本の線の強さ。edgeCoord は伸びる向きの位置、perpCoord は壁からの距離
float LineContrib(float edgeCoord, float perpCoord, float extent, float thickness)
{
    float withinExtent = step(edgeCoord, extent);
    float perpFade = 1.0f - smoothstep(0.0f, thickness, perpCoord);
    float tipDist = extent - edgeCoord;
    float tipGlow = exp(-tipDist / max(thickness * 3.0f, 0.001f)) * perpFade;
    return withinExtent * saturate(perpFade + tipGlow * 1.5f);
}

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }

    float2 uv = PostEffectUV(id.xy);
    float4 original = SampleScreen(uv);

    if (gMaterial.progress <= 0.0f || gMaterial.invasionColor.a <= 0.0f)
    {
        gOutput[id.xy] = original;
        return;
    }

    float progress = gMaterial.progress;
    float thickness = gMaterial.lineThickness;
    float topLine    = LineContrib(uv.x,        uv.y,        progress, thickness);
    float leftLine   = LineContrib(uv.y,        uv.x,        progress, thickness);
    float bottomLine = LineContrib(1.0f - uv.x, 1.0f - uv.y, progress, thickness);
    float rightLine  = LineContrib(1.0f - uv.y, 1.0f - uv.x, progress, thickness);

    float lineMask = saturate(topLine + leftLine + bottomLine + rightLine);
    if (lineMask <= 0.0f)
    {
        gOutput[id.xy] = original;
        return;
    }

    float finalAlpha = lineMask * gMaterial.invasionColor.a;
    float3 blended = lerp(original.rgb, gMaterial.invasionColor.rgb, finalAlpha);
    gOutput[id.xy] = float4(blended, original.a);
}

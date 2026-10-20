#include "SpriteMaterial.hlsli"

// 画面の中央を消失点にした、奥へ流れるサイバー調の格子（加算で重ねる）
struct Params
{
    float4 color;       // 線の色
    float  time;        // 経過時間（スクリプトが進める）
    float  alpha;       // 全体の濃さ
    float  gridScale;   // 格子の細かさ
    float  lineWidth;   // 線の太さ
    float  scrollSpeed; // 流れる速さ
};
ConstantBuffer<Params> gMaterial : register(b0);

float2 PerspectiveGrid(float2 uv, float scale)
{
    float2 centered = uv - 0.5f;
    float perspective = 1.0f / (abs(centered.y) * 2.0f + 0.5f);
    float2 gridUV;
    gridUV.x = centered.x * perspective * scale;
    gridUV.y = centered.y * scale;
    return gridUV;
}

float GridLine(float coord, float lineWidth)
{
    float f = frac(coord);
    float d = min(f, 1.0f - f);
    return 1.0f - smoothstep(0.0f, lineWidth, d);
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 uv = input.texcoord;

    float2 gUV = PerspectiveGrid(uv, gMaterial.gridScale);
    gUV.y += gMaterial.time * gMaterial.scrollSpeed;

    float hLine = GridLine(gUV.x, gMaterial.lineWidth);
    float vLine = GridLine(gUV.y, gMaterial.lineWidth);
    float mask = saturate(hLine + vLine);

    // 消失点の近くは薄くする
    float fadeY = abs(uv.y - 0.5f) * 2.0f;
    mask *= fadeY * fadeY;

    float4 result = gMaterial.color;
    result.a = mask * gMaterial.alpha;
    result.rgb *= mask;
    return result;
}

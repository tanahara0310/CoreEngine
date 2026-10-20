#include "SpriteMaterial.hlsli"

// 選んでいるステージの角丸の枠と、外へ広がって消える残像
struct Params
{
    float4 color;        // 枠の色
    float  borderWidth;  // 枠の太さ（板の縦の UV）
    float  expandT;      // 残像の広がり 0 → 1
    float  alpha;        // 全体の濃さ
    float  innerMargin;  // ステージの縁までの UV の余白
    float  aspectRatio;  // 板の幅 ÷ 高さ
    float  cornerRadius; // 角の丸み（板の縦の UV）
};
ConstantBuffer<Params> gMaterial : register(b0);

static const int   kTrailCount  = 3;
static const float kTrailSpread = 0.08f;

float RoundedRectSDF(float2 p, float2 halfSize, float r)
{
    float2 d = abs(p) - max(halfSize - r, 0.0f);
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - r;
}

float CalcBorderMask(float2 uv, float inner, float bw, float aspect, float radius)
{
    float2 p = (uv - 0.5f) * float2(aspect, 1.0f);
    float2 innerHalf = float2((0.5f - inner) * aspect, 0.5f - inner);
    float sdf = RoundedRectSDF(p, innerHalf, radius);
    return step(-bw, sdf) * (1.0f - step(0.0f, sdf));
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 uv     = input.texcoord;
    float  expand = gMaterial.expandT;
    float  bw     = gMaterial.borderWidth;
    float  margin = gMaterial.innerMargin;
    float  aspect = gMaterial.aspectRatio;
    float  radius = gMaterial.cornerRadius;

    float2 center = float2(0.5f, 0.5f);
    float4 blended = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // 本体の枠
    float bodyMask  = CalcBorderMask(uv, margin, bw, aspect, radius);
    float bodyAlpha = gMaterial.alpha * bodyMask;
    blended.rgb += gMaterial.color.rgb * bodyAlpha;
    blended.a   += bodyAlpha;

    // 残像
    for (int i = 0; i < kTrailCount; i++)
    {
        float phase = (float)(i + 1) / (float)(kTrailCount + 1);
        float trailExpand = saturate(expand + phase * 0.12f);
        float scaleFactor = 1.0f + trailExpand * kTrailSpread;
        float2 scaledUV = (uv - center) / scaleFactor + center;
        float innerUV = margin / scaleFactor;
        float mask = CalcBorderMask(scaledUV, innerUV, bw / scaleFactor, aspect, radius / scaleFactor);
        float trailAlpha = (1.0f - phase * 0.75f)
                         * (1.0f - saturate(trailExpand))
                         * gMaterial.alpha * mask;
        blended.rgb += gMaterial.color.rgb * trailAlpha;
        blended.a   += trailAlpha;
    }

    blended.a   = saturate(blended.a);
    blended.rgb = blended.a > 0.0f ? saturate(blended.rgb / blended.a) : float3(0.0f, 0.0f, 0.0f);
    return blended;
}

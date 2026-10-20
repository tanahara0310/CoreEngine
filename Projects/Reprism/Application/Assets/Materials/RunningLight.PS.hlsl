#include "SpriteMaterial.hlsli"

// 自機とハットの 5×5 の範囲の角丸の枠と、枠に沿って回る光（尾を引いて遅れて付いてくる）
struct Params
{
    float4 baseColor;     // 枠の暗い色
    float4 glowColor;     // 回る光の色
    float2 glowLagOffset; // 遅れて付いてくる中心 − 今の中心（px）
    float  time;          // 経過時間
    float  speed;         // 回る速さ（1 周を 1 として毎秒）
    float  edgeStart;     // 光の位相のずらし
    float  glowSharpness; // 光の鋭さ
    float  thickness;     // 枠の太さ（px）
    float  cornerRadius;  // 角の丸み（px）
    float  glowCount;     // 光の数
    float  halfSpan;      // 枠の半分の幅（px）
    float  drawHalfSpan;  // 板の半分の幅（px）
    float  halfSpanY;     // 枠の半分の高さ（px。0 以下なら halfSpan と同じ）
};
ConstantBuffer<Params> gMaterial : register(b0);

float RoundedBoxSDF(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - r;
}

float2 RoundedBoxNormal(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    if (q.x > 0.0f && q.y > 0.0f)
    {
        float2 pSign = float2(p.x < 0.0f ? -1.0f : 1.0f, p.y < 0.0f ? -1.0f : 1.0f);
        return normalize(pSign * q);
    }
    if (q.x > q.y)
    {
        return float2(sign(p.x), 0.0f);
    }
    return float2(0.0f, sign(p.y));
}

float2 ProjectOntoRoundedRect(float2 p, float2 b, float r)
{
    float sdf = RoundedBoxSDF(p, b, r);
    float2 normal = RoundedBoxNormal(p, b, r);
    return p - sdf * normal;
}

// 角丸の枠を時計回りに 1 周したときの位置（0 → 1）
float RoundedBoxArcUV(float2 p, float2 b, float r)
{
    float2 inner = b - r;
    float totalLen = 4.0f * (inner.x + inner.y) + 2.0f * 3.14159265f * r;

    float topLen   = 2.0f * inner.x;
    float trLen    = 0.5f * 3.14159265f * r;
    float rightLen = 2.0f * inner.y;
    float brLen    = trLen;
    float bottomLen = topLen;
    float blLen    = trLen;
    float leftLen  = rightLen;

    float seg[7];
    seg[0] = topLen;
    seg[1] = seg[0] + trLen;
    seg[2] = seg[1] + rightLen;
    seg[3] = seg[2] + brLen;
    seg[4] = seg[3] + bottomLen;
    seg[5] = seg[4] + blLen;
    seg[6] = seg[5] + leftLen;

    float acc = 0.0f;
    if (p.y >= inner.y && abs(p.x) <= inner.x)
    {
        acc = (p.x + inner.x);
        return acc / totalLen;
    }
    if (p.x >= inner.x && p.y >= inner.y)
    {
        float2 d = p - inner;
        float a = atan2(d.y, d.x);
        a = clamp(a, 0.0f, 1.5707963f);
        acc = seg[0] + (1.5707963f - a) * r;
        return acc / totalLen;
    }
    if (p.x >= inner.x && abs(p.y) <= inner.y)
    {
        acc = seg[1] + (inner.y - p.y);
        return acc / totalLen;
    }
    if (p.x >= inner.x && p.y <= -inner.y)
    {
        float2 d = p - float2(inner.x, -inner.y);
        float a = atan2(d.y, d.x);
        a = clamp(a, -1.5707963f, 0.0f);
        acc = seg[2] + (-a) * r;
        return acc / totalLen;
    }
    if (p.y <= -inner.y && abs(p.x) <= inner.x)
    {
        acc = seg[3] + (inner.x - p.x);
        return acc / totalLen;
    }
    if (p.x <= -inner.x && p.y <= -inner.y)
    {
        float2 d = p - float2(-inner.x, -inner.y);
        float a = atan2(d.y, d.x);
        a = clamp(a, -3.14159265f, -1.5707963f);
        acc = seg[4] + (-1.5707963f - a) * r;
        return acc / totalLen;
    }
    if (p.x <= -inner.x && abs(p.y) <= inner.y)
    {
        acc = seg[5] + (p.y + inner.y);
        return acc / totalLen;
    }
    {
        float2 d = p - float2(-inner.x, inner.y);
        float a = atan2(d.y, d.x);
        a = clamp(a, 1.5707963f, 3.14159265f);
        acc = seg[6] + (3.14159265f - a) * r;
        return acc / totalLen;
    }
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float halfSpan  = gMaterial.halfSpan;
    float halfSpanY = (gMaterial.halfSpanY > 0.0f) ? gMaterial.halfSpanY : halfSpan;
    float drawHalfSpan = max(gMaterial.drawHalfSpan, halfSpan);
    float thickness = gMaterial.thickness;
    float radius    = gMaterial.cornerRadius;

    // 板の UV → 中心を原点にした px
    float drawHalfSpanY = drawHalfSpan * (halfSpanY / max(halfSpan, 0.001f));
    float2 p = (input.texcoord - 0.5f) * float2(drawHalfSpan, drawHalfSpanY) * 2.0f;
    float2 b = float2(halfSpan, halfSpanY);

    // 外と内の SDF で枠の形
    float sdfOuter = RoundedBoxSDF(p, b, radius);
    float sdfInner = RoundedBoxSDF(p, b - thickness, radius - thickness);
    float aa = max(fwidth(sdfOuter), 0.001f);
    float maskOuter = 1.0f - smoothstep(-aa, aa, sdfOuter);
    float maskInner = smoothstep(-aa, aa, sdfInner);
    float mask = maskOuter * maskInner;

    float glowExtra = thickness * 0.6f;

    float2 lagOffset = gMaterial.glowLagOffset;
    int   count = (int)gMaterial.glowCount;
    float glow  = 0.0f;

    if (count > 0)
    {
        float halfWidth = sqrt(1.5f / max(gMaterial.glowSharpness, 1.0f));
        float edge      = halfWidth * 0.2f;

        // 今の位置（t = 0）から遅れた位置（t = 1）まで、枠の上をなぞって尾を描く
        const int kSamples = 12;
        for (int si = 0; si < kSamples; ++si)
        {
            float t = (float)si / (float)(kSamples - 1);
            float2 sampleP = p + lagOffset * t;
            float2 sampleArcP = ProjectOntoRoundedRect(sampleP, b, radius);
            float  sampleArcUV = RoundedBoxArcUV(sampleArcP, b, radius);
            float  sampleWorldUV = frac(sampleArcUV + gMaterial.edgeStart);

            float sdfO = RoundedBoxSDF(sampleP, b, radius);
            float sdfI = RoundedBoxSDF(sampleP, b - (thickness + glowExtra), radius - (thickness + glowExtra));
            float sampleMask = (1.0f - smoothstep(-aa, aa, sdfO)) * smoothstep(-aa, aa, sdfI);

            float fade = (1.0f - t) * (1.0f - t);

            for (int k = 0; k < count; ++k)
            {
                float o  = frac(sampleWorldUV - gMaterial.time * gMaterial.speed - (float)k / (float)count);
                float oc = min(o, 1.0f - o);
                float g  = smoothstep(halfWidth, halfWidth - edge, oc) * fade * sampleMask;
                glow = max(glow, g);
            }
        }
    }

    clip(max(mask, glow) - 0.01f);

    float3 col = lerp(gMaterial.baseColor.rgb, gMaterial.glowColor.rgb, glow);
    float  a   = lerp(gMaterial.baseColor.a,   gMaterial.glowColor.a,   glow);
    float finalMask = max(mask, glow);
    return float4(col, a * finalMask);
}

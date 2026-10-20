#include "SpriteMaterial.hlsli"

// 壊せるブロック：流れる走査線・角の光・縁の光と、踏まれたときの点灯と波紋。
// ハットの範囲（hatRect）の中は描かず、自機の範囲（playerRect）の中は自機の色に染める。
// ghostMode 1 は持ち運び中の写しで、自機の範囲の中を描かない
struct Params
{
    float4 color;            // 地の色
    float4 hatRect;          // xy = ハットの中心（ワールド）／ zw = 半分の大きさ（0 で無効）
    float4 playerRect;       // xy = 自機の中心（ワールド）／ zw = 半分の大きさ（0 で無効）
    float3 accentColor;      // 走査線・角・縁の色
    float  time;             // 経過時間
    float3 hatTintColor;     // 自機の範囲の中の色
    float  scanSpeed;        // 走査線の速さ
    float  scanBrightness;   // 走査線の明るさ
    float  scanWidth;        // 走査線の幅
    float  cornerGlowRadius; // 角の光の届く範囲
    float  cornerGlowPower;  // 角の光の鋭さ
    float  treadBlend;       // 踏まれた光 0 → 1
    float  rippleTime;       // 波紋の経過時間（負で無し）
    float  rippleSpeed;      // 波紋の広がる速さ
    float  rippleWidth;      // 波紋の幅
    float  ghostMode;        // 1 = 持ち運び中の写し
};
ConstantBuffer<Params> gMaterial : register(b0);

float4 MakeTintedBlockColor(float2 uv, float3 tintColor, float baseAlpha)
{
    float scanY = frac(uv.y - gMaterial.time * gMaterial.scanSpeed);
    float halfW = gMaterial.scanWidth * 0.5f;
    float scanMask = smoothstep(0.0f, halfW, scanY) * (1.0f - smoothstep(halfW, gMaterial.scanWidth, scanY));
    float3 scanAdd = scanMask * gMaterial.scanBrightness * tintColor;

    float2 centered = abs(uv - 0.5f);
    float2 fromCorner = max(centered - (0.5f - gMaterial.cornerGlowRadius), 0.0f);
    float cornerDist = length(fromCorner) / gMaterial.cornerGlowRadius;
    float cornerGlow = pow(max(1.0f - cornerDist, 0.0f), gMaterial.cornerGlowPower);
    float3 cornerAdd = cornerGlow * tintColor;

    float edgeDist = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    float borderWidth = 0.07f;
    float borderMask = 1.0f - smoothstep(0.0f, borderWidth, edgeDist);
    float3 borderAdd = borderMask * tintColor * 0.9f;

    float3 tintedRgb = gMaterial.color.rgb + scanAdd + cornerAdd + borderAdd;
    return float4(tintedRgb, baseAlpha);
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 worldPos = input.worldPos.xy;

    float2 playerHalfSize = gMaterial.playerRect.zw;
    if (playerHalfSize.x > 0.0f && playerHalfSize.y > 0.0f)
    {
        float2 playerDelta = abs(worldPos - gMaterial.playerRect.xy);
        const bool inPlayerRect = (playerDelta.x <= playerHalfSize.x && playerDelta.y <= playerHalfSize.y);
        if (inPlayerRect)
        {
            if (gMaterial.ghostMode >= 0.5f)
            {
                discard;
            }
            return MakeTintedBlockColor(input.texcoord, gMaterial.hatTintColor, gMaterial.color.a);
        }
    }

    float2 hatHalfSize = gMaterial.hatRect.zw;
    if (hatHalfSize.x > 0.0f && hatHalfSize.y > 0.0f)
    {
        float2 hatDelta = abs(worldPos - gMaterial.hatRect.xy);
        const bool inHatRect = (hatDelta.x <= hatHalfSize.x && hatDelta.y <= hatHalfSize.y);
        if (inHatRect)
        {
            discard;
        }
    }

    float3 baseRgb = gMaterial.color.rgb;
    float baseA = gMaterial.color.a;
    float2 uv = input.texcoord;

    float scanY = frac(uv.y - gMaterial.time * gMaterial.scanSpeed);
    float halfW = gMaterial.scanWidth * 0.5f;
    float scanMask = smoothstep(0.0f, halfW, scanY) * (1.0f - smoothstep(halfW, gMaterial.scanWidth, scanY));
    float3 scanAdd = scanMask * gMaterial.scanBrightness * gMaterial.accentColor;

    float2 centered = abs(uv - 0.5f);
    float2 fromCorner = max(centered - (0.5f - gMaterial.cornerGlowRadius), 0.0f);
    float cornerDist = length(fromCorner) / gMaterial.cornerGlowRadius;
    float cornerGlow = pow(max(1.0f - cornerDist, 0.0f), gMaterial.cornerGlowPower);
    float3 cornerAdd = cornerGlow * gMaterial.accentColor;

    float edgeDist = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    float borderWidth = 0.07f;
    float borderMask = 1.0f - smoothstep(0.0f, borderWidth, edgeDist);
    float3 borderAdd = borderMask * gMaterial.accentColor * 0.9f;

    float3 treadEdge = borderMask * gMaterial.accentColor * gMaterial.treadBlend;
    float3 treadFill = gMaterial.accentColor * 0.3f * gMaterial.treadBlend;

    float3 rippleAdd = float3(0.0f, 0.0f, 0.0f);
    if (gMaterial.rippleTime >= 0.0f)
    {
        float2 uvCentered = uv - 0.5f;
        float dist = length(uvCentered);
        float rippleRadius = gMaterial.rippleTime * gMaterial.rippleSpeed;
        float rippleDiff = abs(dist - rippleRadius);
        float rippleMask = 1.0f - smoothstep(0.0f, gMaterial.rippleWidth, rippleDiff);
        float fadeOut = 1.0f - saturate(gMaterial.rippleTime * 2.0f);
        rippleAdd = rippleMask * fadeOut * gMaterial.accentColor;
    }

    float3 finalRgb = baseRgb + scanAdd + cornerAdd + borderAdd + treadEdge + treadFill + rippleAdd;
    return float4(finalRgb, baseA);
}

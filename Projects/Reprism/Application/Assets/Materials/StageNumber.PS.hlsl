#include "SpriteMaterial.hlsli"

// ステージ開始の表示（UI 画像）。appearProgress で中央の線から広がって現れ、
// puchunProgress でブラウン管が消えるように潰れて消える
struct Params
{
    float4 color;          // 掛ける色
    float2 uvScale;        // テクスチャの UV の倍率（コマの切り出し）
    float2 uvOffset;       // テクスチャの UV のずらし（コマの切り出し）
    float  puchunProgress; // 消える進み 0 → 1
    float  time;           // 経過時間
    float  appearProgress; // 現れる進み 0 → 1
};
ConstantBuffer<Params> gMaterial : register(b0);

float2 FrameUV(float2 uv)
{
    return uv * gMaterial.uvScale + gMaterial.uvOffset;
}

float Hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float Hash1(float n)
{
    return frac(sin(n) * 43758.5453f);
}

float EaseOutExpo(float t)
{
    return (t >= 1.0f) ? 1.0f : 1.0f - pow(2.0f, -10.0f * t);
}

float EaseInExpo(float t)
{
    return (t <= 0.0f) ? 0.0f : pow(2.0f, 10.0f * t - 10.0f);
}

float ScanlineIntensity(float2 uv, float strength)
{
    float scanWave = sin(uv.y * 3.14159f * 240.0f);
    return 1.0f - strength * (1.0f - scanWave * scanWave);
}

float2 GlitchOffset(float2 uv, float intensity, float time)
{
    float band = floor(uv.y * 12.0f);
    float rnd = Hash(float2(band, floor(time * 18.0f)));
    float trigger = step(0.80f, rnd);
    float offsetX = (rnd * 2.0f - 1.0f) * 0.05f * intensity * trigger;
    float rnd2 = Hash(float2(band + 100.0f, floor(time * 18.0f)));
    float offsetY = (rnd2 * 2.0f - 1.0f) * 0.008f * intensity * step(0.92f, rnd2);
    return float2(offsetX, offsetY);
}

float BlockNoise(float2 uv, float time, float density)
{
    float2 blockUV = floor(uv * 16.0f);
    float rnd = Hash(float2(blockUV.x + floor(time * 24.0f) * 0.3f, blockUV.y));
    return step(density, rnd);
}

float3 ChromaticAberration(float2 uv, float amount)
{
    float2 dir = (uv - 0.5f);
    return float3(
        gTexture.Sample(gSampler, FrameUV(uv + dir * amount)).r,
        gTexture.Sample(gSampler, FrameUV(uv)).g,
        gTexture.Sample(gSampler, FrameUV(uv - dir * amount)).b
    );
}

float DataStream(float2 uv, float time)
{
    float row = floor(uv.y * 32.0f);
    float speed = Hash1(row) * 3.0f + 1.0f;
    float pos = frac(Hash1(row + 10.0f) + time * speed);
    float width = 0.04f + Hash1(row + 20.0f) * 0.06f;
    float streak = smoothstep(width, 0.0f, abs(frac(uv.x - pos) - 0.5f));
    float bright = Hash1(row + floor(time * 8.0f));
    return streak * step(0.6f, bright) * 0.4f;
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 uv = input.texcoord;
    float p = gMaterial.puchunProgress;
    float gTime = gMaterial.time;

    // 現れる段：横へ開く → 縦へ開く → ブロックが埋まる
    if (gMaterial.appearProgress < 1.0f)
    {
        float2 centAp = abs(input.texcoord - 0.5f);

        float tApX     = saturate(gMaterial.appearProgress / 0.40f);
        float eApX     = EaseOutExpo(tApX);
        float halfVisX = lerp(0.0f, 0.5f, eApX);
        if (centAp.x > halfVisX) { discard; }

        float tApY     = saturate((gMaterial.appearProgress - 0.40f) / 0.30f);
        float eApY     = EaseOutExpo(tApY);
        float halfVisY = lerp(0.002f, 0.5f, eApY);
        if (centAp.y > halfVisY) { discard; }

        float tApB = saturate((gMaterial.appearProgress - 0.70f) / 0.30f);
        if (tApB < 1.0f)
        {
            float2 blockUV  = floor(input.texcoord * 16.0f);
            float  blockRnd = Hash(float2(blockUV.x + 77.0f, blockUV.y + 31.0f));
            if (blockRnd > tApB) { discard; }
        }

        float apNoiseX = Hash(float2(floor(gTime * 40.0f), input.texcoord.y * 40.0f));
        float apEdgeX  = (1.0f - smoothstep(halfVisX - 0.015f, halfVisX, centAp.x)) * (0.8f + apNoiseX * 0.5f);
        float apEdgeY  = (1.0f - smoothstep(halfVisY - 0.015f, halfVisY, centAp.y)) * eApY;
        float apGlow   = (apEdgeX + apEdgeY) * 0.6f;

        float apStream = DataStream(input.texcoord, gTime) * saturate(gMaterial.appearProgress * 3.0f) * 0.5f;

        float4 apTex = gTexture.Sample(gSampler, FrameUV(input.texcoord));
        return gMaterial.color * apTex + float4(apGlow + apStream, apGlow + apStream, apGlow + apStream, 0.0f);
    }

    // 消える段：前兆（ブロックの欠けと色ずれ）→ 縦に潰れる → 横に消える
    float glitchStr = lerp(0.1f, 2.5f, saturate(p / 0.30f));
    uv += GlitchOffset(uv, glitchStr, gTime);

    float chromaAmt = lerp(0.003f, 0.06f, saturate(p / 0.30f));
    float3 rgb = ChromaticAberration(uv, chromaAmt);
    float alpha = gTexture.Sample(gSampler, FrameUV(uv)).a;
    float4 texColor = float4(rgb, alpha);

    float scanStr = lerp(0.08f, 0.0f, saturate(p * 3.0f));
    texColor.rgb *= ScanlineIntensity(input.texcoord, scanStr);

    float streamStr = lerp(0.0f, 0.5f, saturate(p / 0.30f));
    texColor.rgb += DataStream(input.texcoord, gTime) * streamStr;

    if (p > 0.0f && p < 0.30f)
    {
        float tPre = p / 0.30f;
        float dropout = BlockNoise(input.texcoord, gTime, lerp(1.0f, 0.6f, tPre));
        if (dropout < 0.5f)
        {
            discard;
        }
        float flashBurst = smoothstep(0.7f, 1.0f, tPre) * Hash1(floor(gTime * 20.0f));
        texColor.rgb += flashBurst * 1.5f;
    }

    if (p >= 0.30f && p < 0.60f)
    {
        float tY = saturate((p - 0.30f) / 0.30f);
        float eY = EaseOutExpo(tY);
        float halfVisibleY = lerp(0.5f, 0.002f, eY);
        float2 cent = abs(input.texcoord - 0.5f);

        float flashT = tY * 3.0f * 3.14159f;
        float flashWave = abs(sin(flashT)) * (1.0f - tY);
        float scanGlow = 1.0f - smoothstep(halfVisibleY, halfVisibleY + 0.015f, cent.y);
        texColor.rgb += scanGlow * (eY * 0.8f + flashWave * 1.5f);

        float edgeNoise = Hash(float2(input.texcoord.x * 50.0f, floor(gTime * 30.0f)));
        float edgeMask = smoothstep(halfVisibleY - 0.02f, halfVisibleY, cent.y);
        texColor.rgb += edgeMask * edgeNoise * eY * 0.8f;

        if (cent.y > halfVisibleY)
        {
            discard;
        }
    }

    if (p >= 0.60f)
    {
        float tX = saturate((p - 0.60f) / 0.40f);
        float eX = EaseInExpo(tX);
        float halfVisibleX = lerp(0.5f, 0.0f, eX);
        float2 cent = abs(input.texcoord - 0.5f);

        float edgeGlow = 1.0f - smoothstep(halfVisibleX, halfVisibleX + 0.015f, cent.x);
        float edgeNoise = Hash(float2(floor(gTime * 40.0f), input.texcoord.y * 40.0f));
        texColor.rgb += edgeGlow * (0.8f + edgeNoise * 0.6f);

        float dissolveRnd = Hash(float2(input.texcoord.y * 30.0f, floor(gTime * 30.0f)));
        float dissolveT = saturate((tX - 0.3f) / 0.7f);
        if (dissolveRnd < dissolveT * 0.8f)
        {
            discard;
        }

        if (cent.x > halfVisibleX)
        {
            discard;
        }
    }

    return gMaterial.color * texColor;
}

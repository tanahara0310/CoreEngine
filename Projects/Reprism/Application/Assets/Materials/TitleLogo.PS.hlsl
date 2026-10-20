#include "SpriteMaterial.hlsli"

// タイトルロゴ：上から写し取るように現れ、グリッチの後は縁が光って呼吸する
struct Params
{
    float4 color;           // ロゴの色
    float  scanProgress;    // 現れた割合 0 → 1
    float  glitchIntensity; // グリッチの強さ 0 → 1
    float  time;            // 経過時間
    float  scanlineSpeed;   // 細かい走査線の流れる速さ
    float  pulseIntensity;  // 光の脈打ち 0 → 1
};
ConstantBuffer<Params> gMaterial : register(b0);

float Hash(float n)
{
    return frac(sin(n) * 43758.5453f);
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 uv = input.texcoord;

    // まだ写し取っていない下側は透明
    float revealMask = step(uv.y, gMaterial.scanProgress);

    // 写し取る線の光（白い芯とマゼンタの光輪）
    float scanEdgeDist = abs(uv.y - gMaterial.scanProgress);
    float scanCore = saturate(1.0f - scanEdgeDist / 0.007f);
    float scanHalo = saturate(1.0f - scanEdgeDist / 0.055f);
    scanHalo = scanHalo * scanHalo;
    float3 scanGlowColor =
        float3(1.0f, 1.0f, 1.0f) * scanCore * 6.0f
      + float3(1.0f, 0.05f, 0.85f) * scanHalo * 3.5f;

    // グリッチ：帯ごとに横へずらす
    float cellY = floor(uv.y * 24.0f);
    float randVal = Hash(cellY + floor(gMaterial.time * 80.0f));
    float glitchUV = uv.x + (randVal - 0.5f) * gMaterial.glitchIntensity * 0.10f;
    float2 sampUV = float2(saturate(glitchUV), uv.y);
    float noiseBlock = step(0.92f, Hash(cellY * 7.3f + gMaterial.time * 60.0f)) * gMaterial.glitchIntensity;

    // 脈打ちに合わせて色を横へずらす
    float chromaStr = gMaterial.pulseIntensity * 0.007f;
    float rVal = gTexture.Sample(gSampler, float2(saturate(sampUV.x + chromaStr), sampUV.y)).r;
    float gVal = gTexture.Sample(gSampler, sampUV).g;
    float bVal = gTexture.Sample(gSampler, float2(saturate(sampUV.x - chromaStr), sampUV.y)).b;
    float4 texColor = gTexture.Sample(gSampler, sampUV);
    float3 chromaRGB = float3(rVal, gVal, bVal);

    // 文字の縁だけをマゼンタで光らせる
    float innerGlow = texColor.a * texColor.a * (1.0f - texColor.a);
    float3 neonEmission = float3(1.0f, 0.08f, 0.9f) * innerGlow * 2.5f;

    // 細かい走査線（現れていない所には掛けない）
    float scanlinePos = uv.y - gMaterial.time * gMaterial.scanlineSpeed;
    float scanlineWave = sin(scanlinePos * 120.0f) * 0.025f + 1.0f;
    scanlineWave = lerp(1.0f, scanlineWave, revealMask);

    float3 finalRGB = (chromaRGB + scanGlowColor + noiseBlock * 0.4f + neonEmission) * scanlineWave;
    float finalA = texColor.a * revealMask * gMaterial.color.a;
    return float4(finalRGB * gMaterial.color.rgb, finalA);
}

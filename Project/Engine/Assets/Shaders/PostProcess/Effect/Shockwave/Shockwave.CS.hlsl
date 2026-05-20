// Shockwave.CS.hlsl - ショックウェーブ コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ShockwaveParams : register(b0)
{
    float2 center;    // 中心座標 (0-1)
    float time;       // 時間累積
    float strength;   // 強度
    float thickness;  // 波の太さ
    float speed;      // 波の速度
    float2 padding;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float2 uv = float2(
        (float)dispatchId.x / (float)screenWidth,
        (float)dispatchId.y / (float)screenHeight
    );

    float2 toCenter = uv - center;
    float dist = length(toCenter);

    float waveRadius = time * speed;
    float waveDiff = abs(dist - waveRadius);
    float waveEffect = 1.0f - smoothstep(0.0f, thickness, waveDiff);

    float staticDistortion = strength * 0.1f * (1.0f - smoothstep(0.0f, 0.5f, dist));
    float dynamicDistortion = waveEffect * strength;
    float totalDistortion = staticDistortion + dynamicDistortion;

    float4 color;
    if (totalDistortion > 0.001f)
    {
        float2 distortion = normalize(toCenter) * totalDistortion;
        float2 distortedUV = saturate(uv + distortion);
        int2 sampleCoord = clamp(
            (int2)(distortedUV * float2(screenWidth, screenHeight)),
            int2(0, 0),
            int2((int)screenWidth - 1, (int)screenHeight - 1)
        );
        color = gTexture.Load(int3(sampleCoord, 0));
    }
    else
    {
        color = gTexture.Load(int3(dispatchId.xy, 0));
    }

    gOutput[dispatchId.xy] = color;
}

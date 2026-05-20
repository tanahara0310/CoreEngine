// ChromaticAberration.CS.hlsl - 色収差 コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ChromaticAberrationParams : register(b0)
{
    float intensity;       // 色収差の強度 (0.0 to 20.0)
    float radialFactor;    // 放射状の強度 (0.0 to 3.0)
    float centerX;         // 中心X座標
    float centerY;         // 中心Y座標

    float distortionScale; // 歪みスケール (0.0 to 5.0)
    float falloff;         // 端に近づく強度の減衰 (0.1 to 5.0)
    float samples;         // 未使用（互換性用）
    float padding;
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
    float2 center = float2(centerX, centerY);
    float2 direction = uv - center;
    float dist = length(direction);
    float2 normalizedDir = dist > 0.0001f ? direction / dist : float2(0.0f, 0.0f);

    float radialIntensity = pow(saturate(dist * radialFactor), falloff);
    float edgeDistance = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    float edgeFactor = 1.0f - smoothstep(0.0f, 0.3f, edgeDistance);
    float aberrationStrength = intensity * 0.001f * distortionScale * (radialIntensity + edgeFactor * 0.3f);

    float4 originalColor = gTexture.Load(int3(dispatchId.xy, 0));

    if (aberrationStrength < 0.00001f)
    {
        gOutput[dispatchId.xy] = originalColor;
        return;
    }

    // RGB チャンネルを異なる距離でサンプリング（テクセル座標で計算）
    float2 texelSize = float2(1.0f / (float)screenWidth, 1.0f / (float)screenHeight);
    float redDist   =  aberrationStrength * 1.5f;
    float blueDist  = -aberrationStrength * 1.5f;

    float2 redUV   = uv + normalizedDir * redDist;
    float2 greenUV = uv;
    float2 blueUV  = uv + normalizedDir * blueDist;

    // テクセル座標に変換
    int2 redCoord   = clamp((int2)(redUV   * float2(screenWidth, screenHeight)), int2(0,0), int2(screenWidth-1, screenHeight-1));
    int2 greenCoord = int2(dispatchId.xy);
    int2 blueCoord  = clamp((int2)(blueUV  * float2(screenWidth, screenHeight)), int2(0,0), int2(screenWidth-1, screenHeight-1));

    float r = gTexture.Load(int3(redCoord,   0)).r;
    float g = gTexture.Load(int3(greenCoord, 0)).g;
    float b = gTexture.Load(int3(blueCoord,  0)).b;

    gOutput[dispatchId.xy] = float4(r, g, b, originalColor.a);
}

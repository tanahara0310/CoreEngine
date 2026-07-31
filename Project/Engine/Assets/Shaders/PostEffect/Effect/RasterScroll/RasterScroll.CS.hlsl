// RasterScroll.CS.hlsl - ラスタースクロール コンピュートシェーダー

#include "ShaderMath.hlsli" // TWO_PI

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer RasterScrollParams : register(b0)
{
    float scrollSpeed;         // スクロール速度
    float lineHeight;          // ライン高さ
    float amplitude;           // 振幅
    float frequency;           // 周波数

    float time;                // 時間
    float lineOffset;          // ライン開始位置オフセット
    float distortionStrength;  // 歪み強度
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

    // 波形歪みの計算
    float wavePhase      = (uv.y + lineOffset) * frequency * TWO_PI;
    float horizontalWave = sin(wavePhase + time * scrollSpeed) * amplitude;
    float detailWave1    = sin(uv.y * lineHeight * 2.0f + time * scrollSpeed * 1.5f) * amplitude * 0.3f;
    float detailWave2    = sin(uv.y * lineHeight * 0.5f + time * scrollSpeed * 0.7f) * amplitude * 0.5f;
    float verticalWave   = sin(uv.x * frequency * 4.0f + time * scrollSpeed * 0.8f) * amplitude * 0.15f;
    float diagonalWave   = sin((uv.x + uv.y) * frequency * 2.0f + time * scrollSpeed * 1.2f) * amplitude * 0.2f;

    float2 totalDistortion = float2(
        horizontalWave + detailWave1 + detailWave2 + diagonalWave * 0.5f,
        verticalWave + diagonalWave * 0.3f
    ) * distortionStrength;

    float2 distortedUV = saturate(uv + totalDistortion);

    // エッジフェード
    float2 edgeMask = smoothstep(0.0f, 0.05f, distortedUV) * smoothstep(1.0f, 0.95f, distortedUV);
    float edgeFactor = edgeMask.x * edgeMask.y;
    float2 finalUV = lerp(uv, distortedUV, edgeFactor);

    // テクセル座標に変換してサンプリング
    int2 sampleCoord = clamp(
        (int2)(finalUV * float2(screenWidth, screenHeight)),
        int2(0, 0),
        int2((int)screenWidth - 1, (int)screenHeight - 1)
    );
    float4 color = gTexture.Load(int3(sampleCoord, 0));

    // スキャンライン効果
    float scanlinePhase = uv.y * lineHeight * 2.0f + time * scrollSpeed * 0.3f;
    float scanlineEffect = 1.0f + 0.03f * sin(scanlinePhase) + 0.02f * sin(scanlinePhase * 2.7f);
    float contrastBoost = 1.0f + 0.05f * sin(uv.y * frequency + time * scrollSpeed * 0.5f);
    color.rgb *= scanlineEffect * contrastBoost;

    // 歪みによる微細な色変化
    float distortionIntensity = length(totalDistortion);
    float colorShift = sin(distortionIntensity * 10.0f + time * scrollSpeed) * 0.02f;
    color.rgb += float3(colorShift, -colorShift * 0.5f, colorShift * 0.3f);

    gOutput[dispatchId.xy] = color;
}

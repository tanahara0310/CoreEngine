// Blur.CS.hlsl - ガウシアンブラー コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer BlurParams : register(b0)
{
    float intensity; // ブラー強度 (0.0-5.0)
    float kernelSize; // カーネルサイズ (0.5-3.0)
    float2 padding;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

groupshared float4 sharedPixels[kGroupSize + 6][kGroupSize + 6]; // 最大radius=3のパディング

[numthreads(kGroupSize, kGroupSize, 1)]
void main(
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 dispatchId : SV_DispatchThreadID)
{
    // 境界外チェック
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    int kernelRadius = max(1, (int) (kernelSize * 2.0f));
    kernelRadius = min(kernelRadius, 3); // groupsharedサイズに合わせた上限

    float2 texelSize = float2(1.0f / (float) screenWidth, 1.0f / (float) screenHeight);

    // groupsharedに周辺ピクセルを読み込む
    int2 sharedBase = (int2) (groupId.xy * kGroupSize) - int2(kernelRadius, kernelRadius);
    uint sharedWidth = kGroupSize + 2 * kernelRadius;
    uint sharedHeight = kGroupSize + 2 * kernelRadius;

    uint flatId = groupThreadId.y * kGroupSize + groupThreadId.x;
    uint totalShared = sharedWidth * sharedHeight;

    [loop]
    for (uint i = flatId; i < totalShared; i += kGroupSize * kGroupSize)
    {
        int2 sharedCoord = int2(i % sharedWidth, i / sharedWidth);
        int2 texCoord = sharedBase + sharedCoord;
        texCoord = clamp(texCoord, int2(0, 0), int2((int) screenWidth - 1, (int) screenHeight - 1));
        sharedPixels[sharedCoord.y][sharedCoord.x] = gTexture.Load(int3(texCoord, 0));
    }

    GroupMemoryBarrierWithGroupSync();

    // ガウシアンブラー計算（groupsharedから読む）
    float4 color = float4(0, 0, 0, 0);
    float totalWeight = 0.0f;

    for (int x = -kernelRadius; x <= kernelRadius; x++)
    {
        for (int y = -kernelRadius; y <= kernelRadius; y++)
        {
            int2 sharedCoord = (int2) groupThreadId.xy + int2(kernelRadius + x, kernelRadius + y);
            float dist = length(float2(x, y));
            float weight = exp(-dist * dist / (2.0f * kernelSize * kernelSize));

            color += sharedPixels[sharedCoord.y][sharedCoord.x] * weight;
            totalWeight += weight;
        }
    }

    color /= totalWeight;

    // 元の色とブレンド
    int2 baseCoord = (int2) (groupId.xy * kGroupSize) + (int2) groupThreadId.xy;
    float4 originalColor = gTexture.Load(int3(baseCoord, 0));
    gOutput[dispatchId.xy] = lerp(originalColor, color, intensity);
}

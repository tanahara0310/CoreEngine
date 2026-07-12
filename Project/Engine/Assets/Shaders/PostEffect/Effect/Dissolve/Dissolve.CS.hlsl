// Dissolve.CS.hlsl - ディゾルブ コンピュートシェーダー

Texture2D<float4> inputTexture : register(t0);
Texture2D<float4> noiseTexture : register(t1);
RWTexture2D<float4> gOutput : register(u0);

cbuffer DissolveParams : register(b0)
{
    float threshold; // ディゾルブ閾値 (0.0-1.0)
    float edgeWidth; // エッジ幅 (0.0-0.5)
    float edgeColorR; // エッジカラー R
    float edgeColorG; // エッジカラー G
    float edgeColorB; // エッジカラー B
    float3 padding;
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

    float4 baseColor = inputTexture.Load(int3(dispatchId.xy, 0));

    // ノイズテクスチャはサイズが異なる可能性があるためUV変換してからLoad
    uint noiseW, noiseH;
    noiseTexture.GetDimensions(noiseW, noiseH);
    float2 uv = float2(
        (float) dispatchId.x / (float) screenWidth,
        (float) dispatchId.y / (float) screenHeight
    );
    int2 noiseCoord = int2(uv * float2(noiseW, noiseH));
    noiseCoord = clamp(noiseCoord, int2(0, 0), int2(noiseW - 1, noiseH - 1));
    float noiseValue = noiseTexture.Load(int3(noiseCoord, 0)).r;

    // ディゾルブ閾値以下のピクセルは透明に
    if (noiseValue < threshold)
    {
        gOutput[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // エッジのブレンド
    float edgeFactor = smoothstep(threshold, threshold + edgeWidth, noiseValue);
    float3 edgeColor = float3(edgeColorR, edgeColorG, edgeColorB);
    float3 finalColor = lerp(edgeColor, baseColor.rgb, edgeFactor);

    gOutput[dispatchId.xy] = float4(finalColor, baseColor.a);
}

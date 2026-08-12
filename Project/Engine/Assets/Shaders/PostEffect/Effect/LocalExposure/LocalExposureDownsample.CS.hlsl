// LocalExposureDownsample.CS.hlsl - 対数輝度の 1/8 解像度ベース層を作る
//
// ローカル露出は「画面の低周波の明暗分布」だけを圧縮したい。
// まずシーンの対数輝度を 1/8 解像度へ落とし、後段の分離ガウシアンで
// 大きくぼかしてベース層（局所の基準明るさ）にする。

#include "ColorSpace.hlsli" // Luminance

Texture2D<float4> gTexture : register(t0); // シーンカラー（リニアHDR）
RWTexture2D<float> gOutput : register(u0); // 対数輝度（1/8解像度）

cbuffer DownsampleParams : register(b0)
{
    uint2 outputSize; // 1/8 解像度
    uint2 sourceSize; // フル解像度
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
    {
        return;
    }

    // 出力 1 テクセルに対応するソース 8x8 ブロックの対数輝度平均（幾何平均）。
    // 線形平均だと 1 つの輝点がブロック全体を「明るい」判定にしてしまう
    const uint2 blockOrigin = dispatchId.xy * 8;
    float sum = 0.0f;
    for (uint y = 0; y < 8; ++y)
    {
        for (uint x = 0; x < 8; ++x)
        {
            const uint2 p = min(blockOrigin + uint2(x, y), sourceSize - 1);
            const float lum = Luminance(gTexture.Load(int3(p, 0)).rgb);
            sum += log2(max(lum, 1e-6f));
        }
    }

    gOutput[dispatchId.xy] = sum / 64.0f;
}

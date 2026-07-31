// LuminanceReduction.CS.hlsl - 自動露出用の平均輝度計測
// シーンカラー（トーンマッピング入力のリニアHDR値）を 64x64 の均等グリッドで
// サンプリングし、線形輝度の算術平均を 1 要素のバッファへ出力する。
// 1 スレッドグループのみで実行する（Dispatch(1,1,1)）。
//
// 対数平均（幾何平均）ではなく算術平均を使う理由:
// 幾何平均はゼロ近傍のピクセルに過敏で、「真っ暗な地面 + 薄明の空」のような
// シーンで平均が桁違いに小さくなり、露出が空を昼のように持ち上げてしまう。
// カメラの平均測光と同じ算術平均なら暗部の面積に引きずられにくく、
// 空（明部）の絶対的な明るさが露出を支配する。

#include "ColorSpace.hlsli" // Luminance

Texture2D<float4> gTexture : register(t0);
RWStructuredBuffer<float> gAvgLuminance : register(u0);

// ToneMapping.CS.hlsl と同じレイアウト（同じ定数バッファを共有する）
cbuffer ScreenParams : register(b0)
{
    uint screenWidth;
    uint screenHeight;
    float exposureEV;
    float pad;
};

static const uint kGroupSize = 16;          // 16x16 = 256 スレッド
static const uint kSamplesPerThread = 4;    // 各スレッド 4x4 点 → 全体 64x64 = 4096 サンプル
static const uint kGridSize = kGroupSize * kSamplesPerThread; // 64

groupshared float sLogLumSum[kGroupSize * kGroupSize];

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    // 画面全域を均等に覆う 64x64 グリッドから対数輝度を収集する。
    // 全画素を読むより桁違いに軽く、露出計測には十分な標本数。
    float sum = 0.0f;
    for (uint j = 0; j < kSamplesPerThread; ++j)
    {
        for (uint i = 0; i < kSamplesPerThread; ++i)
        {
            const uint2 grid = uint2(
                groupThreadId.x * kSamplesPerThread + i,
                groupThreadId.y * kSamplesPerThread + j); // 0..63
            const float2 uv = (float2(grid) + 0.5f) / kGridSize;
            uint2 pixel = uint2(uv * float2(screenWidth, screenHeight));
            pixel = min(pixel, uint2(screenWidth - 1, screenHeight - 1));

            const float3 color = gTexture.Load(int3(pixel, 0)).rgb;
            const float luminance = Luminance(color);

            // 太陽ディスク等の極端な高輝度ピクセルが数サンプルで平均を支配しないよう上限を設ける
            sum += clamp(luminance, 0.0f, 64.0f);
        }
    }
    sLogLumSum[groupIndex] = sum;
    GroupMemoryBarrierWithGroupSync();

    // groupshared 上の並列リダクション
    for (uint stride = (kGroupSize * kGroupSize) / 2; stride > 0; stride >>= 1)
    {
        if (groupIndex < stride)
        {
            sLogLumSum[groupIndex] += sLogLumSum[groupIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
    {
        gAvgLuminance[0] = sLogLumSum[0] / (kGridSize * kGridSize);
    }
}

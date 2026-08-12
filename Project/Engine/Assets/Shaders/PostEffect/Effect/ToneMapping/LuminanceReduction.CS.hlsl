// LuminanceReduction.CS.hlsl - 自動露出用の測光（ヒストグラム百分位トリム平均）
// シーンカラー（トーンマッピング入力のリニアHDR値）を 64x64 の均等グリッドで
// サンプリングし、対数輝度ヒストグラムを作り、百分位カットした範囲の
// 平均輝度を 1 要素のバッファへ出力する。
// 1 スレッドグループのみで実行する（Dispatch(1,1,1)）。
//
// 【なぜヒストグラム + 百分位カットか】
// 単純平均は外れ値に弱い。太陽ディスクや強い水面グリッターが画面へ入ると
// 数サンプルで平均が跳ね上がり、全体が沈む→外れると戻る「呼吸」が出る。
// 下側 lowPercentile・上側 highPercentile の外を捨てることで、
// 極端な高輝度（太陽）と大面積の暗部（影の地面）の両方が測光を支配しなくなる。
// low=0 / high=1 にすると従来の平均測光とほぼ等価（ビン中心近似の誤差のみ）。
//
// 平均は算術平均（線形輝度）。幾何平均はゼロ近傍に過敏で
// 「真っ暗な地面 + 薄明の空」で露出が空を昼のように持ち上げてしまう（従来からの教訓）。

#include "ColorSpace.hlsli" // Luminance

Texture2D<float4> gTexture : register(t0);
RWStructuredBuffer<float> gAvgLuminance : register(u0);

// ToneMapping.CS.hlsl と同じレイアウト（同じ定数バッファを共有する）
cbuffer ScreenParams : register(b0)
{
    uint screenWidth;
    uint screenHeight;
    float exposureEV;
    uint toneMapOperator;
};

cbuffer HistogramParams : register(b1)
{
    float lowPercentile;  // この割合より暗いサンプルを捨てる（0.5 = 下位50%）
    float highPercentile; // この割合より明るいサンプルを捨てる（0.9 = 上位10%）
    float2 histogramPad;
};

static const uint kGroupSize = 16;          // 16x16 = 256 スレッド
static const uint kSamplesPerThread = 4;    // 各スレッド 4x4 点 → 全体 64x64 = 4096 サンプル
static const uint kGridSize = kGroupSize * kSamplesPerThread; // 64
static const uint kTotalSamples = kGridSize * kGridSize;

// ヒストグラムの対数輝度レンジ。下限 2^-12（月明かり以下）〜上限 2^6=64（従来のクランプ値と同じ）
static const uint kBinCount = 64;
static const float kMinLogLum = -12.0f;
static const float kMaxLogLum = 6.0f;

groupshared uint sHistogram[kBinCount];

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    // ビンのクリア（256 スレッド中、先頭 64 スレッドが担当）
    if (groupIndex < kBinCount)
    {
        sHistogram[groupIndex] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // 画面全域を均等に覆う 64x64 グリッドから輝度を収集してビンへ積む
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

            const float luminance = Luminance(gTexture.Load(int3(pixel, 0)).rgb);
            const float logLum = log2(max(luminance, 1e-6f));
            const float t = saturate((logLum - kMinLogLum) / (kMaxLogLum - kMinLogLum));
            const uint bin = min(uint(t * kBinCount), kBinCount - 1);
            InterlockedAdd(sHistogram[bin], 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 百分位トリム平均（64 ビンの逐次走査は 1 スレッドで十分軽い）
    if (groupIndex == 0)
    {
        const float lowCount = lowPercentile * kTotalSamples;
        const float highCount = highPercentile * kTotalSamples;

        float sum = 0.0f;
        float weight = 0.0f;
        float cumulative = 0.0f;
        for (uint b = 0; b < kBinCount; ++b)
        {
            const float count = float(sHistogram[b]);
            const float binStart = cumulative;
            cumulative += count;

            // このビンのうち [lowCount, highCount] 区間に入る個数だけ採用する
            // （境界ビンは部分的に採用され、百分位が滑らかに効く）
            const float used = max(min(cumulative, highCount) - max(binStart, lowCount), 0.0f);
            if (used > 0.0f)
            {
                const float binCenter =
                    exp2(kMinLogLum + (float(b) + 0.5f) * (kMaxLogLum - kMinLogLum) / kBinCount);
                sum += used * binCenter;
                weight += used;
            }
        }

        // 全サンプルが捨てられた場合（low >= high の設定ミス等）は中間グレーへ逃がす
        gAvgLuminance[0] = (weight > 0.0f) ? (sum / weight) : 0.18f;
    }
}

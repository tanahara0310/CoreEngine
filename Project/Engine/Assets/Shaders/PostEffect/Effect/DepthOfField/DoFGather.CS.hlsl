// DoFGather.CS.hlsl - ディスクギャザー（散乱の集約近似）
//
// 「各画素の色が自分の CoC 半径の円盤に散乱する」のが物理だが、散乱は GPU で高くつく。
// 代わりに各画素が周囲をサンプリングし、「そのタップのボケ円盤が自分を覆うか」で
// 重み付けして集める（scatter-as-gather）。
// タップ位置は Vogel 螺旋 + 画素ごとの回転ジッタで、少ないサンプル数のバンディングを散らす。

Texture2D<float4> gPrefiltered : register(t0); // rgb=色 / a=符号付きCoC[ハーフ解像度px]
RWTexture2D<float4> gOutput : register(u0);    // rgb=ボケ色 / a=中心の|CoC|

cbuffer DoFGatherParams : register(b0)
{
    uint2 textureSize;  // ハーフ解像度
    float maxCocHalfPx; // CoC 上限[ハーフ解像度px] = ギャザー半径
    uint sampleCount;
};

/// @brief Interleaved Gradient Noise（他のパスと同じ関数）
float InterleavedGradientNoise(float2 pixelPosition)
{
    return frac(52.9829189f * frac(dot(pixelPosition, float2(0.06711056f, 0.00583715f))));
}

static const uint kGroupSize = 8;
static const float kGoldenAngle = 2.39996323f;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= textureSize.x || dispatchId.y >= textureSize.y)
    {
        return;
    }

    const int2 coord = int2(dispatchId.xy);
    const float4 center = gPrefiltered.Load(int3(coord, 0));
    const float centerCoc = center.a;

    const float rotation = InterleavedGradientNoise(float2(coord)) * 6.2831853f;

    // 中心の自己寄与（覆域 1 扱い）
    float3 sum = center.rgb;
    float weightSum = 1.0f;

    for (uint i = 0; i < sampleCount; ++i)
    {
        // Vogel 螺旋: 円盤を均等に覆うサンプル列
        const float r = sqrt((float(i) + 0.5f) / float(sampleCount)) * maxCocHalfPx;
        const float theta = float(i) * kGoldenAngle + rotation;
        const int2 offset = int2(round(float2(cos(theta), sin(theta)) * r));
        const int2 p = clamp(coord + offset, int2(0, 0), int2(textureSize) - 1);

        const float4 tap = gPrefiltered.Load(int3(p, 0));
        const float dist = length(float2(offset));

        // タップのボケ円盤が中心を覆う度合い（±1px のソフトエッジ）
        float w = saturate(abs(tap.a) - dist + 1.0f);

        // 背景（正のCoC）がシャープな前景の上へ滲み出すのを抑える:
        // 中心より奥のタップは、中心自身のボケ量を超える寄与を持てない
        if (tap.a > centerCoc + 1.0f)
        {
            w *= saturate(abs(centerCoc) - dist + 1.0f);
        }

        sum += tap.rgb * w;
        weightSum += w;
    }

    gOutput[coord] = float4(sum / weightSum, abs(centerCoc));
}

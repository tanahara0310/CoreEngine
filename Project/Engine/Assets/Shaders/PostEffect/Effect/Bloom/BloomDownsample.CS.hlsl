// BloomDownsample.CS.hlsl - ブルームのダウンサンプル（1/2 解像度へ）
//
// 入力の 4x4 ボックスを 1 テクセルへ畳む。2x2 ではなく 4x4 にしているのは、
// 1 テクセルだけ極端に明るい画素（スペキュラのハイライト等）が残ると
// カメラが動いたときにブルームがちらつくため。
// Load のみで組み、サンプラーは使わない（このエンジンのポストエフェクトの流儀）。

#include "ColorSpace.hlsli" // Luminance

Texture2D<float4>   gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer BloomDownsampleParams : register(b0)
{
    uint2 outputSize;     // 出力（このパス）の解像度
    uint2 sourceSize;     // 入力の解像度
    float threshold;      // 輝度閾値
    float softKnee;       // ソフトニー [0,1]
    uint  applyPrefilter; // 1 のときだけ閾値処理を行う（チェーンの最初の 1 回）
    float padding;
};

static const uint kGroupSize = 8;

// ソフトスレッショルド（閾値の境目を滑らかにして、明滅を抑える）
float SoftThreshold(float lum, float thresh, float knee)
{
    float k = thresh * knee;
    float soft = lum - thresh + k;
    soft = clamp(soft, 0.0f, 2.0f * k);
    soft = soft * soft / (4.0f * k + 0.00001f);
    float contribution = max(soft, lum - thresh);
    contribution /= max(lum, 0.00001f);
    return contribution;
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
    {
        return;
    }

    const int2 maxCoord = int2((int)sourceSize.x - 1, (int)sourceSize.y - 1);
    const int2 base = (int2)dispatchId.xy * 2;

    float3 sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 2; ++y)
    {
        [unroll]
        for (int x = -1; x <= 2; ++x)
        {
            int2 coord = clamp(base + int2(x, y), int2(0, 0), maxCoord);
            sum += gSource.Load(int3(coord, 0)).rgb;
        }
    }
    float3 color = sum * (1.0f / 16.0f);

    if (applyPrefilter != 0)
    {
        // 閾値は最初のダウンサンプルでだけ掛ける。各段で掛けると
        // ぼかすほど輝度が下がって二重に削られる
        float lum = Luminance(color);
        color *= SoftThreshold(lum, threshold, softKnee);
    }

    gOutput[dispatchId.xy] = float4(color, 1.0f);
}

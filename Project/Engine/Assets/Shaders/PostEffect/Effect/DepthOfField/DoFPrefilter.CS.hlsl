// DoFPrefilter.CS.hlsl - ハーフ解像度化と錯乱円（CoC）の計算
//
// 薄レンズモデル: 焦点面から外れた点光源はセンサー上で円（錯乱円）に写る。
//   CoC = 開口径 A × f × |d - fd| / (d × (fd - f))
// CPU 側で「(d - fd)/d に掛ける係数」まで前計算してあるので、シェーダーは 1 式で済む。
// 符号: 正 = 焦点面より奥（背景ボケ）、負 = 手前（前ボケ）

#include "ColorSpace.hlsli"

Texture2D<float4> gTexture : register(t0); // フル解像度シーンカラー
Texture2D<float> gDepth : register(t1);    // シーン深度（NDC [0,1]）
RWTexture2D<float4> gOutput : register(u0); // rgb=色 / a=符号付き CoC[ハーフ解像度px]

cbuffer DoFPrefilterParams : register(b0)
{
    uint2 outputSize;    // ハーフ解像度
    uint2 fullSize;      // フル解像度
    float focusDistance; // 焦点面までの距離[m]
    float cocScalePx;    // CoC 係数[フル解像度px]（CPU 前計算）
    float maxCocPx;      // CoC の上限[フル解像度px]
    float nearPlane;
    float farPlane;
    float3 pad;
};

float LinearDepth(float ndcDepth)
{
    return (nearPlane * farPlane) / (farPlane - ndcDepth * (farPlane - nearPlane));
}

/// @brief 符号付き CoC [フル解像度px]
float ComputeCoc(float viewDepth)
{
    const float coc = cocScalePx * (viewDepth - focusDistance) / max(viewDepth, 1e-3f);
    return clamp(coc, -maxCocPx, maxCocPx);
}

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
    {
        return;
    }

    // 2x2 ブロックの平均色と、ブロック内で最も「ボケの強い」CoC を取る
    const uint2 base = dispatchId.xy * 2;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    float coc = 0.0f;
    float maxAbsCoc = -1.0f;
    for (uint y = 0; y < 2; ++y)
    {
        for (uint x = 0; x < 2; ++x)
        {
            const uint2 p = min(base + uint2(x, y), fullSize - 1);
            color += gTexture.Load(int3(p, 0)).rgb;
            const float c = ComputeCoc(LinearDepth(gDepth.Load(int3(p, 0))));
            if (abs(c) > maxAbsCoc)
            {
                maxAbsCoc = abs(c);
                coc = c;
            }
        }
    }

    // CoC はハーフ解像度ピクセル単位へ変換して持つ（ギャザーの距離と単位を揃える）
    gOutput[dispatchId.xy] = float4(color * 0.25f, coc * 0.5f);
}

// ToneMapping.CS.hlsl - ACES トーンマッピング コンピュートシェーダー

#include "ColorSpace.hlsli" // ACESFilm

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ScreenParams : register(b0)
{
    uint screenWidth;
    uint screenHeight;
    float exposureEV; // 露出補正 [EV]。ACES 適用前に exp2(EV) を乗算（0 = 従来動作）
    float pad;
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float4 color = gTexture.Load(int3(dispatchId.xy, 0));

    // 露出補正（トーンカーブ適用前のリニア HDR 値に対して行う）
    color.rgb *= exp2(exposureEV);

    // HDR → LDR 変換
    color.rgb = ACESFilm(color.rgb);

    // NOTE: ガンマ補正（リニア→sRGB）はここで行わない。
    // バックバッファのRTVフォーマットが DXGI_FORMAT_R8G8B8A8_UNORM_SRGB のため、
    // GPUがハードウェアで自動的に変換を行う。ここで行うと二重補正になる。

    gOutput[dispatchId.xy] = color;
}

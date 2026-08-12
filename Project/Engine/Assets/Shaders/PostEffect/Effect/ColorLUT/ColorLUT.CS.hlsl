// ColorLUT.CS.hlsl - 3D LUT カラーグレーディング
//
// DaVinci Resolve 等で作った .cube をそのまま適用する。
// LUT は「表示される画像」（sRGB エンコード後）を基準に作られるのが普通なので、
// リニア LDR → sRGB へ寄せてから引き、結果をリニアへ戻す。
// チェーン内の値はリニアのまま流れる（sRGB エンコードは最終出力の RTV ハードウェア変換）。
//
// トーンマップ直後（PostTonemap 先頭）で適用すること。
// HDR 値に LDR で作った LUT を掛けると範囲外で壊れる。

#include "ColorSpace.hlsli" // LinearToSRGB / SRGBToLinear

Texture2D<float4> gTexture : register(t0);
Texture3D<float4> gLut : register(t1);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ColorLUTParams : register(b0)
{
    uint screenWidth;
    uint screenHeight;
    uint lutSize;
    float blend; // 0 = 元画像 / 1 = LUT 全適用
};

/// @brief 手動トライリニア補間（既存の流儀に合わせてサンプラー不使用）
float3 SampleLutTrilinear(float3 srgb)
{
    const float scale = float(lutSize - 1);
    const float3 pos = saturate(srgb) * scale;
    const int3 p0 = int3(floor(pos));
    const int3 p1 = min(p0 + int3(1, 1, 1), int3(lutSize - 1, lutSize - 1, lutSize - 1));
    const float3 f = pos - float3(p0);

    const float3 c000 = gLut.Load(int4(p0.x, p0.y, p0.z, 0)).rgb;
    const float3 c100 = gLut.Load(int4(p1.x, p0.y, p0.z, 0)).rgb;
    const float3 c010 = gLut.Load(int4(p0.x, p1.y, p0.z, 0)).rgb;
    const float3 c110 = gLut.Load(int4(p1.x, p1.y, p0.z, 0)).rgb;
    const float3 c001 = gLut.Load(int4(p0.x, p0.y, p1.z, 0)).rgb;
    const float3 c101 = gLut.Load(int4(p1.x, p0.y, p1.z, 0)).rgb;
    const float3 c011 = gLut.Load(int4(p0.x, p1.y, p1.z, 0)).rgb;
    const float3 c111 = gLut.Load(int4(p1.x, p1.y, p1.z, 0)).rgb;

    const float3 c00 = lerp(c000, c100, f.x);
    const float3 c10 = lerp(c010, c110, f.x);
    const float3 c01 = lerp(c001, c101, f.x);
    const float3 c11 = lerp(c011, c111, f.x);
    const float3 c0 = lerp(c00, c10, f.y);
    const float3 c1 = lerp(c01, c11, f.y);
    return lerp(c0, c1, f.z);
}

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    const int2 coord = int2(dispatchId.xy);
    const float4 color = gTexture.Load(int3(coord, 0));

    const float3 srgb = LinearToSRGB(color.rgb);
    const float3 graded = SampleLutTrilinear(srgb);
    const float3 result = SRGBToLinear(graded);

    gOutput[coord] = float4(lerp(color.rgb, result, blend), color.a);
}

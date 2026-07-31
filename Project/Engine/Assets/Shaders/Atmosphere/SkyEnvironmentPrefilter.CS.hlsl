/// @file SkyEnvironmentPrefilter.CS.hlsl
/// @brief 空キューブマップ（空＋雲）を GGX プリフィルタしてスペキュラIBLミップ群を生成する
/// @details IBL/PrefilterEnvironment.CS.hlsl のランタイム軽量版。
///          - 雲アニメーション中は毎フレーム走るため、サンプル数を 64 に抑える
///            （入力の空は低周波なのでこれで十分。雲の高周波はミップのブラーで均される）
///          - α（雲透過率）も同じ重みでフィルタする。水面が「平面反射に雲を
///            ブレンドする不透明度」として使うため（Water.PS.hlsl 参照）。
///          評価側は mip = roughness × (ミップ数-1) で SampleLevel する。

#include "Sampling.hlsli" // PI / Hammersley / ImportanceSampleGGX
#include "Cubemap.hlsli"  // GetCubemapDirection

static const uint SAMPLE_COUNT = 64u;

TextureCube<float4> gSkyCubemap : register(t0);   // 入力: 空＋雲キューブマップ（mip0 のみ）
RWTexture2DArray<float4> gSkySpecularMap : register(u0); // 出力: 対象ミップの 6 面
SamplerState gLUTSampler : register(s0);

cbuffer SkyPrefilterParams : register(b0)
{
    float gRoughness;   // 現在のミップに対応するラフネス (0.0-1.0)
    float gPad0;
    uint2 gOutputSize;  // 出力ミップの解像度
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gOutputSize.x || dtid.y >= gOutputSize.y || dtid.z >= 6)
    {
        return;
    }

    const float2 uv = (float2(dtid.xy) + 0.5f) / float2(gOutputSize);
    const float3 N = GetCubemapDirection(dtid.z, uv);

    // mip0（鏡面）は入力をそのままコピーする
    const float MIN_ROUGHNESS = 0.01f;
    if (gRoughness < MIN_ROUGHNESS)
    {
        gSkySpecularMap[dtid] = gSkyCubemap.SampleLevel(gLUTSampler, N, 0.0f);
        return;
    }

    // R = N = V 近似（Split-Sum の標準前提）
    const float3 R = N;
    const float3 V = R;

    float4 prefiltered = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, gRoughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL > 0.0f)
        {
            // 入力は 1 ミップのみのため SampleLevel(…, 0) 固定。
            // 空は低周波・雲は後段ミップのブラーで均されるためエイリアシングは実用上出ない
            prefiltered += gSkyCubemap.SampleLevel(gLUTSampler, L, 0.0f) * NdotL;
            totalWeight += NdotL;
        }
    }

    gSkySpecularMap[dtid] = (totalWeight > 0.0f) ? prefiltered / totalWeight : prefiltered;
}

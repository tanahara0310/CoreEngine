/// @file CloudCubemapCapture.CS.hlsl
/// @brief 空キューブマップへの雲の焼き込み（スペキュラ IBL / 水面の雲反射）
/// @details SkyEnvironmentCapture.CS.hlsl が書いた空キューブマップの各テクセルへ、
///          メインビューと同じ MarchClouds で雲を前乗算合成する。
///          レイはキューブマップ面方向（カメラ位置起点）で、不透明ジオメトリ遮蔽は無い。
///          反復予算はメインビューの半分（品質差はミップで均される）。
///          雲は風で毎フレーム動くため、雲が有効なフレームでは毎回実行される。

#include "Common/CloudMarch.hlsli"
#include "Cubemap.hlsli" // GetCubemapDirection

RWTexture2DArray<float4> gSkyCubemap : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width, height, faces;
    gSkyCubemap.GetDimensions(width, height, faces);
    if (dtid.x >= width || dtid.y >= height || dtid.z >= 6)
    {
        return;
    }

    // ===== レイ生成（キューブマップ面方向・カメラ位置起点） =====
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(width, height);
    float3 rayDir = GetCubemapDirection(dtid.z, uv);
    float3 rayOrigin = gCloud.cameraWorldPos;

    // ===== マーチ区間（雲層シェル） =====
    float2 interval = CloudLayerInterval(rayOrigin, rayDir, gCloud);
    float marchStart = interval.x;
    float marchEnd = min(interval.y, gCloud.maxMarchDistanceM);
    if (marchStart >= marchEnd)
    {
        return; // 雲層と交差しない方向は空のまま
    }

    float ign = InterleavedGradientNoise(float2(dtid.xy + dtid.z * 64u));

    CloudMarchResult cloud = MarchClouds(rayOrigin, rayDir, marchStart, marchEnd,
                                         max(gCloud.maxSteps, 1u), ign);

    // ===== 空キューブマップへ前乗算合成（雲色 + 空色 × 透過率） =====
    float4 sky = gSkyCubemap[dtid];
    gSkyCubemap[dtid] = float4(cloud.luminance + sky.rgb * cloud.transmittance,
                               sky.a * cloud.transmittance);
}

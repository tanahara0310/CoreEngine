/// @file AerialPerspective.CS.hlsl
/// @brief 空気遠近感のシーン合成（Aerial Perspective 適用）
/// @details SceneColor と SceneDepth を読み、深度から求めた距離で
///          CameraVolume LUT をサンプリングして霞を合成する。
///          出力先は中間テクスチャ（パスが SceneColor へコピーバックする）。
///          式: 出力 = シーン色 × 透過率 + inscattering

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture3D<float4> gCameraVolumeLUT : register(t2);
SamplerState gLUTSampler : register(s0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gOutput.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    float4 sceneColor = gSceneColor[dispatchThreadId.xy];
    float ndcDepth = gSceneDepth[dispatchThreadId.xy];

    // 深度が far（空・背景）のピクセルには適用しない（空は Sky-View LUT で完成済み）
    // 注意: far クリップが大きい場合（例 50km）、数 km 先の不透明物でも深度は 0.9999 を
    // 超えるため、しきい値は far 面（クリア値 1.0）だけを弾く極小マージンにする。
    if (ndcDepth >= 0.9999999f)
    {
        gOutput[dispatchThreadId.xy] = sceneColor;
        return;
    }

    // 深度 → ワールド座標 → カメラからの距離 [km]
    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(width, height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float4 worldPos = mul(float4(ndc, ndcDepth, 1.0f), gAtmosphere.invViewProj);
    worldPos /= worldPos.w;

    float distanceMeters = length(worldPos.xyz - gAtmosphere.cameraWorldPos);
    float distanceKm = distanceMeters * 0.001f;

    // CameraVolume LUT から (inscattering, 透過率) を取得して合成
    float w = CameraVolumeDistanceToW(distanceKm);
    float4 aerial = gCameraVolumeLUT.SampleLevel(gLUTSampler, float3(uv, w), 0);

    float3 inscattering = aerial.rgb * gAtmosphere.sunColor * gAtmosphere.sunIntensity;
    float transmittance = aerial.a;

    gOutput[dispatchThreadId.xy] = float4(
        sceneColor.rgb * transmittance + inscattering,
        sceneColor.a);
}

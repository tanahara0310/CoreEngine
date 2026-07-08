/// @file CameraVolumeLUT.CS.hlsl
/// @brief Camera Volume (Aerial Perspective) LUT の生成（Hillaire 2020）
/// @details froxel 状 3D テクスチャ。各 froxel = (スクリーンUV, 距離スライス) に対する
///          「カメラからその距離までの inscattering (rgb) と平均透過率 (a)」。
///          シーン合成時（AerialPerspective.CS.hlsl）は深度から距離を求めて
///          この LUT をトライリニアサンプリングするだけになる。

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gTransmittanceLUT : register(t0);
Texture2D<float4> gMultiScatteringLUT : register(t1);
SamplerState gLUTSampler : register(s0);
RWTexture3D<float4> gCameraVolumeLUT : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= CAMERA_VOLUME_SIZE ||
        dispatchThreadId.y >= CAMERA_VOLUME_SIZE ||
        dispatchThreadId.z >= CAMERA_VOLUME_SIZE)
    {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(CAMERA_VOLUME_SIZE, CAMERA_VOLUME_SIZE);

    // スクリーンUV → ワールド視線方向（逆ビュープロジェクション）
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float4 farWorld = mul(float4(ndc, 1.0f, 1.0f), gAtmosphere.invViewProj);
    farWorld /= farWorld.w;
    float3 rayDir = normalize(farWorld.xyz - gAtmosphere.cameraWorldPos);

    // スライス距離（テクセル中心 = (z+0.5)×スライス幅。サンプリング側の
    // CameraVolumeDistanceToW と半テクセルずれなく対応させる）
    float tMaxKm = (float(dispatchThreadId.z) + 0.5f) * CAMERA_VOLUME_KM_PER_SLICE;

    // 大気空間: カメラは +Y 軸上。方向ベクトルはワールドと共通（Y-up）
    float3 rayOrigin = float3(0.0f, gAtmosphere.cameraRadiusKm, 0.0f);
    float3 toSun = -gAtmosphere.sunDirection;

    const int kStepCount = 16;
    float3 transmittance;
    float3 luminance = IntegrateScatteredLuminanceToDistance(
        rayOrigin, rayDir, toSun, tMaxKm, gAtmosphere,
        gTransmittanceLUT, gMultiScatteringLUT, gLUTSampler, kStepCount, transmittance);

    // 透過率は輝度への寄与平均で1チャンネル化（UE と同じ近似）
    float meanTransmittance = dot(transmittance, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));

    gCameraVolumeLUT[dispatchThreadId] = float4(luminance, meanTransmittance);
}

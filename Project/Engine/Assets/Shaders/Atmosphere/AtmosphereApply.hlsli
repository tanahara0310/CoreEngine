/// @file AtmosphereApply.hlsli
/// @brief 半透明パス向けの Aerial Perspective 適用ヘルパー（UEギャップ解消 Phase 6）
/// @details 不透明への合成（AerialPerspective.CS.hlsl）はスクリーン全体へ深度ベースで
///          一括適用されるが、その後に描かれるフォワード半透明（水面等）には効かない。
///          半透明 PS の末尾で ApplyAerialPerspective() を呼び、ピクセル自身の距離で
///          同じ霞を適用する。
///          インクルード側が用意するリソース（他のレジスタと衝突しないよう固定割当）:
///            b6  = AtmosphereConstants（AtmosphereManager の定数バッファ）
///            t22 = CameraVolume LUT（Texture3D）
///            t23 = Sky-View LUT（CameraVolume 最大距離超のフォールバック用）
///          サンプラーは CLAMP のものを呼び出し側から渡すこと（LUT は WRAP 禁止。
///          Transmittance 系 LUT と同じ理由で、範囲外が反対端へ巻き込むため）。

#ifndef ATMOSPHERE_APPLY_HLSLI
#define ATMOSPHERE_APPLY_HLSLI

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphereAP : register(b6);
Texture3D<float4> gCameraVolumeLUT : register(t22);
Texture2D<float4> gSkyViewLUTAP : register(t23);

/// @brief ピクセル色へ空気遠近感（霞）を適用する
/// @param color 適用前のピクセル色（リニア HDR）
/// @param worldPos ピクセルのワールド座標 [m]
/// @param screenUv スクリーン UV（CameraVolume は GameView の froxel なので描画先と同じ UV）
/// @param lutClampSampler CLAMP アドレッシングのリニアサンプラー
float3 ApplyAerialPerspective(float3 color, float3 worldPos, float2 screenUv, SamplerState lutClampSampler)
{
    float3 toPixel = worldPos - gAtmosphereAP.cameraWorldPos;
    float distanceMeters = length(toPixel);
    float distanceKm = distanceMeters * 0.001f;

    // CameraVolume LUT から (inscattering, 透過率) を取得して合成。
    // 式は不透明への合成（AerialPerspective.CS.hlsl）と完全に一致させること
    float w = CameraVolumeDistanceToW(distanceKm, gAtmosphereAP.apKmPerSlice);
    float4 aerial = gCameraVolumeLUT.SampleLevel(lutClampSampler, float3(screenUv, w), 0);
    float3 result = color * aerial.a
                  + aerial.rgb * gAtmosphereAP.sunColor * gAtmosphereAP.sunIntensity;

    // ===== CameraVolume の最大距離を超える遠方: Sky-View LUT へ溶かす =====
    // LUT は w=1 で頭打ちになり、それ以上遠くても霞が濃くならない。
    // 雲の遠方フェード（CloudRayMarch.CS.hlsl）と同じ 60km 時定数の消散近似で
    // 空の色そのものへ収束させる
    float maxDistKm = gAtmosphereAP.apKmPerSlice * (float)CAMERA_VOLUME_SIZE;
    if (distanceKm > maxDistKm)
    {
        float3 viewDir = toPixel / max(distanceMeters, 1e-3f);
        float radiusKm = clamp(gAtmosphereAP.cameraRadiusKm,
            gAtmosphereAP.planetRadiusKm + 0.001f,
            gAtmosphereAP.atmosphereTopRadiusKm - 0.001f);

        // SkyAtmosphere.PS.hlsl と同じ UV 算出（天頂角＋太陽相対方位角）
        float viewZenithCos = viewDir.y;
        float3 toSun = -gAtmosphereAP.sunDirection;
        float2 viewHorizontal = viewDir.xz;
        float2 sunHorizontal = toSun.xz;
        float viewHorizontalLen = length(viewHorizontal);
        float sunHorizontalLen = length(sunHorizontal);
        float lightViewCos = 1.0f;
        if (viewHorizontalLen > 1e-5f && sunHorizontalLen > 1e-5f)
        {
            lightViewCos = dot(viewHorizontal / viewHorizontalLen, sunHorizontal / sunHorizontalLen);
        }
        float cosHorizon = -sqrt(max(0.0f,
            1.0f - (gAtmosphereAP.planetRadiusKm * gAtmosphereAP.planetRadiusKm) / (radiusKm * radiusKm)));
        bool intersectGround = (viewZenithCos < cosHorizon);
        float2 skyUv = SkyViewParamsToUv(intersectGround, viewZenithCos, lightViewCos,
                                         radiusKm, gAtmosphereAP.planetRadiusKm);
        float3 skyLum = gSkyViewLUTAP.SampleLevel(lutClampSampler, skyUv, 0).rgb
                      * gAtmosphereAP.sunColor * gAtmosphereAP.sunIntensity;

        float blend = 1.0f - exp(-(distanceKm - maxDistKm) / 60.0f);
        result = lerp(result, skyLum, blend);
    }

    return result;
}

#endif // ATMOSPHERE_APPLY_HLSLI

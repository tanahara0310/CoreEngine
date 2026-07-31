/// @file CloudCubemapCapture.CS.hlsl
/// @brief 空キューブマップへの雲の焼き込み（Phase 3b: スペキュラIBL / 水面の雲反射）
/// @details SkyEnvironmentCapture.CS.hlsl が書いた空キューブマップの各テクセルへ、
///          CloudRayMarch.CS.hlsl と同じ物理ベースレイマーチで雲を前乗算合成する。
///          - レイ生成のみ画面ピクセルではなくキューブマップ面方向（カメラ位置起点）
///          - 不透明ジオメトリ遮蔽（SceneDepth）は無し（空の映り込み用途のため不要）
///          - 出力 α は雲の透過率（雲なし=1）。水面が平面反射との合成不透明度に使う
///          解像度は 64²×6 = 24.6k レイで、メインビュー半解像度レイマーチの数%程度。
///          雲は風で毎フレーム動くため、雲が有効なフレームでは毎回実行される。

#include "Common/CloudCommon.hlsli"
#include "../Atmosphere/Common/AtmosphereCommon.hlsli"
#include "Cubemap.hlsli" // GetCubemapDirection

ConstantBuffer<CloudConstants> gCloud : register(b0);
ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b1);
Texture3D<float4> gBaseShapeNoise : register(t0);
Texture3D<float4> gDetailNoise : register(t1);
Texture2D<float4> gWeatherMap : register(t2);
Texture2D<float4> gTransmittanceLUT : register(t4);
Texture2D<float4> gSkyViewLUT : register(t5);
SamplerState gSamplerLinearWrap : register(s0);
SamplerState gLUTSampler : register(s1); // RootSignature 側で LinearClamp 固定
RWTexture2DArray<float4> gSkyCubemap : register(u0);

/// @brief 雲内部の 1 点における指定ライト由来の輝度（CloudRayMarch.CS.hlsl と同一ロジック）
float3 DirectLightLuminanceAt(float3 pos, float3 rayDir,
                              float3 lightDirection, float3 lightColor, float lightIntensity)
{
    float3 toSun = -lightDirection;

    // 雲自身によるセルフシャドウ（指数ステップのサンライトマーチ）
    float densitySum = 0.0f;
    float stepLen = gCloud.lightMarchStepM;
    float distAcc = 0.0f;
    [unroll] for (int j = 0; j < 6; ++j)
    {
        distAcc += stepLen;
        float3 sp = pos + toSun * distAcc;
        float sh = CloudHeightFraction(sp, gCloud);
        densitySum += SampleCloudDensity(sp, sh, true, 0.0f, gCloud,
            gBaseShapeNoise, gDetailNoise, gWeatherMap, gSamplerLinearWrap) * stepLen;
        stepLen *= 2.0f;
    }

    float tauSun = min(densitySum * gCloud.densityScale, 8.0f);
    float cosTheta = dot(rayDir, toSun);

    // 多重散乱の近似（Hillaire の N オクターブ法）
    float3 energy = float3(0.0f, 0.0f, 0.0f);
    float attenuation = 1.0f;
    float contribution = 1.0f;
    float eccentricity = 1.0f;

    [unroll] for (int o = 0; o < 3; ++o)
    {
        float phase = lerp(HenyeyGreensteinPhase(gCloud.phaseG0 * eccentricity, cosTheta),
                           HenyeyGreensteinPhase(gCloud.phaseG1 * eccentricity, cosTheta),
                           gCloud.phaseBlend);

        float tau = tauSun * attenuation;
        float beer = exp(-tau);
        float powder = 1.0f - exp(-tau * 2.0f);
        float lightEnergy = lerp(beer, beer * powder * 2.0f, gCloud.beerPowderStrength * 0.5f);

        energy += contribution * phase * lightEnergy;

        attenuation *= gCloud.msAttenuation;
        contribution *= gCloud.msContribution;
        eccentricity *= gCloud.msEccentricity;
    }

    energy *= gCloud.sunLightScale;

    // 大気透過率（夕暮れに雲が赤くなる要因。惑星中心基準の局所高度を含む）
    float3 offsetKm = (pos - gCloud.cameraWorldPos) * 0.001f;
    float3 posAtmo = float3(offsetKm.x, gAtmosphere.cameraRadiusKm + offsetKm.y, offsetKm.z);
    float3 sunTrans = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler,
                                               posAtmo, toSun, gAtmosphere);

    return sunTrans * energy * lightColor * lightIntensity;
}

/// @brief 雲内部の 1 点における直接光（太陽＋月）の合計輝度（CloudRayMarch.CS.hlsl と同一）
float3 SunLuminanceAt(float3 pos, float3 rayDir)
{
    float3 luminance = DirectLightLuminanceAt(pos, rayDir,
        gCloud.sunDirection, gCloud.sunColor, gCloud.sunIntensity);
    if (gCloud.hasMoon > 0.5f)
    {
        luminance += DirectLightLuminanceAt(pos, rayDir,
            gCloud.moonDirection, gCloud.moonColor, gCloud.moonIntensity);
    }
    return luminance;
}

/// @brief 雲内部の 1 点における空由来のアンビエント輝度（CloudRayMarch.CS.hlsl と同一）
float3 AmbientLuminance(float h)
{
    // 方位は太陽から 90°の固定値。LUT はライト色・強度前乗算済みのため色乗算はしない
    float radiusKm = gAtmosphere.cameraRadiusKm;
    float azimuth = SkyViewAzimuth(-gAtmosphere.sunDirection) + 0.5f * PI;
    float2 uv = SkyViewParamsToUv(false, 0.7f, azimuth, radiusKm, gAtmosphere.planetRadiusKm);
    float3 skyLum = gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb;

    float gray = dot(skyLum, float3(0.333f, 0.333f, 0.334f));
    skyLum = lerp(float3(gray, gray, gray), skyLum, 0.35f);

    return skyLum * gCloud.ambientIntensity * lerp(0.45f, 1.0f, h);
}

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

    // ===== 積分（CloudRayMarch.CS.hlsl と同じエネルギー保存マーチ） =====
    float baseFine = max(gCloud.layerThicknessM / 128.0f, 4.0f);
    // 反射・IBL 用途なので反復予算はメインビューの半分に抑える（品質差はミップで均される）
    uint maxIter = max(gCloud.maxSteps, 1u);

    float ign = InterleavedGradientNoise(float2(dtid.xy + dtid.z * 64u));

    float proj = dot(rayOrigin, rayDir);
    float k = ceil((marchStart + proj) / baseFine - ign);
    float tFirst = (k + ign) * baseFine - proj;
    float t = tFirst;

    float3 color = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;

    float weightedDist = 0.0f;
    float weightSum = 0.0f;

    bool inCloud = false;
    int emptyRun = 0;

    [loop] for (uint i = 0; i < maxIter; ++i)
    {
        if (t >= marchEnd || transmittance < gCloud.earlyExitTransmittance)
        {
            break;
        }

        uint stride = 1u + uint(5.0f * saturate((t - 6000.0f) / 24000.0f)
                              + 3.0f * saturate((t - 30000.0f) / 25000.0f)
                              + 7.0f * saturate((t - 55000.0f) / 45000.0f));
        float dtFine = baseFine * float(stride);
        float dtBig = dtFine * 4.0f;
        float distanceAlongRay = t - marchStart;

        float3 pos = rayOrigin + rayDir * t;
        float hf = CloudHeightFraction(pos, gCloud);

        if (!inCloud)
        {
            float probe = SampleCloudDensity(pos, hf, true, 0.0f, gCloud,
                gBaseShapeNoise, gDetailNoise, gWeatherMap, gSamplerLinearWrap);
            if (probe > 0.0f)
            {
                inCloud = true;
                emptyRun = 0;
                t = max(t - dtBig, tFirst);
                continue;
            }
            t += dtBig;
            continue;
        }

        float detailFade = saturate(1.0f - distanceAlongRay / 25000.0f);
        float density = SampleCloudDensity(pos, hf, false,
            gCloud.detailErosionStrength * detailFade, gCloud,
            gBaseShapeNoise, gDetailNoise, gWeatherMap, gSamplerLinearWrap);

        density *= saturate((gCloud.maxMarchDistanceM - t) / 8000.0f);

        if (density > 0.0f)
        {
            emptyRun = 0;

            float sigmaS = density * gCloud.densityScale;
            float sigmaE = max(sigmaS, 1e-7f);
            float stepTrans = exp(-sigmaE * dtFine);

            float3 luminance = SunLuminanceAt(pos, rayDir) + AmbientLuminance(hf);
            float3 S = sigmaS * luminance;
            float3 Sint = (S - S * stepTrans) / sigmaE;

            color += transmittance * Sint;

            float stepAlpha = transmittance * (1.0f - stepTrans);
            weightedDist += stepAlpha * t;
            weightSum += stepAlpha;

            transmittance *= stepTrans;
        }
        else if (++emptyRun > 8)
        {
            inCloud = false;
        }

        t += dtFine;
    }

    if (transmittance < gCloud.earlyExitTransmittance)
    {
        transmittance = 0.0f;
    }

    // ===== 空気遠近（遠方の雲を空へ溶かす。CloudRayMarch.CS.hlsl と同一近似） =====
    float alpha = 1.0f - transmittance;
    if (alpha > 0.001f && weightSum > 1e-5f)
    {
        float cloudDist = weightedDist / weightSum;

        float radiusKm = clamp(gAtmosphere.cameraRadiusKm,
            gAtmosphere.planetRadiusKm + 0.001f,
            gAtmosphere.atmosphereTopRadiusKm - 0.001f);
        float cosHorizon = -sqrt(max(0.0f,
            1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
        // 方位は太陽から 90°の固定値。LUT はライト色・強度前乗算済みのため色乗算はしない
        float hazeAzimuth = SkyViewAzimuth(-gAtmosphere.sunDirection) + 0.5f * PI;
        float2 skyUv = SkyViewParamsToUv(rayDir.y < cosHorizon, rayDir.y, hazeAzimuth,
                                         radiusKm, gAtmosphere.planetRadiusKm);
        float3 skyLum = gSkyViewLUT.SampleLevel(gLUTSampler, skyUv, 0).rgb;

        float haze = 1.0f - exp(-cloudDist / 60000.0f);
        color = lerp(color, skyLum * alpha, haze);
    }

    // ===== 空キューブマップへ前乗算合成（雲色 + 空色 × 透過率） =====
    float4 sky = gSkyCubemap[dtid];
    gSkyCubemap[dtid] = float4(color + sky.rgb * transmittance, sky.a * transmittance);
}

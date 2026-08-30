/// @file CloudCirrus.hlsli
/// @brief 高層の巻雲を 1 枚の薄いシェルとして解く
/// @details 積雲層のようにマーチせず、球殻との交点 1 つで密度・透過率・輝度を求める。
///          光学的に薄いので単一散乱で足り、反復が要らない。

#ifndef CLOUD_CIRRUS_HLSLI
#define CLOUD_CIRRUS_HLSLI

#include "CloudMarch.hlsli"

/// @brief 巻雲シェル上の点のサンプル座標
/// @details 風方向へ強く縮めると、その方向へ模様が伸びて筋雲になる。
///          巻雲は下層より速く流れるので移流速度に倍率を掛ける。
float2 CirrusSamplePos(float2 worldXZ)
{
    float2 wind = float2(gCloud.windDirX, gCloud.windDirZ);
    float windLen = length(wind);
    float2 w = (windLen > 1e-4f) ? wind / windLen : float2(1.0f, 0.0f);

    float2 advected = worldXZ + w * (gCloud.windSpeedMPerS * gCloud.cirrusWindScale * gCloud.timeSec);
    float2 aligned = float2(dot(advected, w) / max(gCloud.cirrusStretch, 1.0f),
                            dot(advected, float2(-w.y, w.x)));
    return aligned / max(gCloud.cirrusScaleM, 1.0f);
}

/// @brief 巻雲の散乱エネルギー（位相関数のオクターブ和）
/// @details 単一散乱の HG だけだと側方・後方で積雲の 1/5 の輝度になり、
///          白い筋雲にならない。積雲と同じ多重散乱オクターブで等方成分を足す。
float CirrusScatterEnergy(float cosTheta)
{
    float energy = 0.0f;
    float contribution = 1.0f;
    float eccentricity = 1.0f;

    [unroll] for (int o = 0; o < kCloudMultiScatterOctaves; ++o)
    {
        float phase = lerp(kCloudIsotropicPhase,
                           HenyeyGreensteinPhase(kCirrusPhaseG, cosTheta), eccentricity);
        energy += contribution * phase;
        contribution *= gCloud.msContribution;
        eccentricity *= gCloud.msEccentricity;
    }
    return energy;
}

/// @brief 巻雲シェルを解いて前乗算輝度と透過率を返す
/// @param rayOrigin レイ始点（ワールド [m]）
/// @param rayDir レイ方向（正規化）
/// @param maxDistanceM 不透明ジオメトリまでの距離。これより遠い交点は見えない
/// @details カバレッジ 0 のときは何も無いものとして返す。
CloudMarchResult SampleCirrusShell(float3 rayOrigin, float3 rayDir, float maxDistanceM)
{
    CloudMarchResult result;
    result.luminance = float3(0.0f, 0.0f, 0.0f);
    result.transmittance = 1.0f;
    result.distance = -1.0f;

    if (gCloud.cirrusCoverage <= 0.0f)
    {
        return result;
    }

    // 球殻との交点。カメラは殻の内側にあるので出口側が可視の交点になる
    float3 center = CloudPlanetCenter(gCloud);
    float radius = gCloud.planetRadiusM + gCloud.cirrusAltitudeM;
    float2 hit = RaySphere(rayOrigin, rayDir, center, radius);
    float t = hit.y;
    if (t <= 0.0f || t >= min(gCloud.maxMarchDistanceM, maxDistanceM))
    {
        return result;
    }

    // 地平線より下は地表が手前にあるので殻は見えない（単一シェルなので内殻の判定が無い）
    if (RaySphere(rayOrigin, rayDir, center, gCloud.planetRadiusM).x > 0.0f)
    {
        return result;
    }

    float3 pos = rayOrigin + rayDir * t;
    float2 p = CirrusSamplePos(pos.xz);

    // 水平スライスを 2 段の周波数で引く。粗い側は尾根状（|n| の反転）にして筋を立てる。
    // 素の値をしきい値で切ると塊になり、筋雲ではなく一様なベールに見える
    float4 coarse = gBaseShapeNoise.SampleLevel(gSamplerLinearWrap, float3(p.x, 0.37f, p.y), 0);
    float ridged = 1.0f - abs(coarse.r * 2.0f - 1.0f);

    float4 fine = gBaseShapeNoise.SampleLevel(gSamplerLinearWrap,
        float3(p.x * kCirrusFineFrequency + 0.13f, 0.71f, p.y * kCirrusFineFrequency + 0.41f), 0);
    float fineFbm = fine.g * 0.6f + fine.b * 0.4f;

    // 筋を細かいノイズで途切れさせる。連続した尾根のままだと編み目模様になる
    float shape = ridged * saturate(Remap(fineFbm, kCirrusFineCutoff, 0.85f, 0.0f, 1.0f));

    float coverage = saturate(gCloud.cirrusCoverage);
    float density = saturate(Remap(shape, 1.0f - coverage, 1.0f, 0.0f, 1.0f));

    // 交点が遠いほど視線が浅く、殻を斜めに長く貫くので光学的深さが伸びる
    float slantScale = min(rcp(max(abs(rayDir.y), kCirrusMinSlantCos)), kCirrusMaxSlant);

    // 最大マーチ距離の手前でフェードし、殻が地平線で唐突に切れないようにする
    density *= saturate((gCloud.maxMarchDistanceM - t) / gCloud.farFadeWidthM);

    float tau = density * gCloud.cirrusDensity * slantScale;
    if (tau <= 1e-4f)
    {
        return result;
    }

    result.transmittance = exp(-tau);
    result.distance = t;

    // ===== 単一散乱（氷晶なので前方散乱が強い） =====
    float alpha = 1.0f - result.transmittance;
    float3 offsetKm = (pos - gCloud.cameraWorldPos) * 0.001f;
    float3 posAtmo = float3(offsetKm.x, gAtmosphere.cameraRadiusKm + offsetKm.y, offsetKm.z);

    float3 direct = float3(0.0f, 0.0f, 0.0f);
    {
        float3 toSun = -gCloud.sunDirection;
        float3 trans = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler, posAtmo, toSun, gAtmosphere);
        direct += trans * gCloud.sunColor * gCloud.sunIntensity * CirrusScatterEnergy(dot(rayDir, toSun));
    }
    if (gCloud.hasMoon > 0.5f)
    {
        float3 toMoon = -gCloud.moonDirection;
        float3 trans = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler, posAtmo, toMoon, gAtmosphere);
        direct += trans * gCloud.moonColor * gCloud.moonIntensity * CirrusScatterEnergy(dot(rayDir, toMoon));
    }

    // 層の上端なので空はほぼ全方位見える
    float3 ambient = CloudAmbientLuminance(1.0f) * kCirrusAmbientScale;

    result.luminance = (direct * gCloud.sunLightScale + ambient) * alpha;
    return result;
}

/// @brief 2 層を前後関係で合成する（back が奥）
CloudMarchResult CompositeCloudLayers(CloudMarchResult front, CloudMarchResult back)
{
    CloudMarchResult result;
    result.luminance = front.luminance + back.luminance * front.transmittance;
    result.transmittance = front.transmittance * back.transmittance;
    // 時間再投影の履歴 UV は手前の層を優先する
    result.distance = (front.distance > 0.0f) ? front.distance : back.distance;
    return result;
}

#endif // CLOUD_CIRRUS_HLSLI

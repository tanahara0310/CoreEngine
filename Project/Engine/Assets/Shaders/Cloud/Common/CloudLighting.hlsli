/// @file CloudLighting.hlsli
/// @brief 雲内部の 1 点における直接光とアンビエントの輝度
/// @details 輝度ドメインは空（SkyAtmosphere.PS）と共通。
///          セルフシャドウのサンライトマーチ + Beer-Powder + 二重ローブ HG 位相関数 +
///          Transmittance LUT による太陽色 + Sky-View LUT によるアンビエント。

#ifndef CLOUD_LIGHTING_HLSLI
#define CLOUD_LIGHTING_HLSLI

#include "CloudMarchBindings.hlsli"

/// @brief 大気 LUT のパラメータ化が破綻しない範囲へ丸めたカメラ半径 [km]
float CloudSafeCameraRadiusKm()
{
    return clamp(gAtmosphere.cameraRadiusKm,
        gAtmosphere.planetRadiusKm + 0.001f,
        gAtmosphere.atmosphereTopRadiusKm - 0.001f);
}

/// @brief Sky-View LUT を引く方位角（太陽から 90°）
/// @note ヘイズもアンビエントも仰角による明るさ変化が支配的で、方位の精度は要らない
float CloudSkyViewAzimuth()
{
    return SkyViewAzimuth(-gAtmosphere.sunDirection) + 0.5f * PI;
}

/// @brief 雲内部の 1 点における指定ディレクショナルライト由来の輝度（太陽・月共用）
/// @param pos ワールド座標 [m]
/// @param rayDir 視線方向（位相関数用）
/// @param lightDirection 光の進行方向（正規化）
/// @param lightColor ライト色
/// @param lightIntensity ライト強度（大気の輝度ドメイン）
/// @details セルフシャドウは「指数的に伸びる固定ステップ」でマーチする（Schneider/Nubis 標準）。
///          近傍を密に・遠方を粗くサンプルするため、ジッタ無しでも滑らかな遮蔽が得られる。
///          ここへジッタを入れると exp() の急峻さで増幅され、斜めドット格子が雲面に焼き付く。
float3 CloudDirectLightLuminance(float3 pos, float3 rayDir,
                                 float3 lightDirection, float3 lightColor, float lightIntensity)
{
    float3 toSun = -lightDirection; // lightDirection は光の進行方向

    // 光源方向へ指数ステップでマーチし、光学的深さを積む
    float densitySum = 0.0f;
    float stepLen = gCloud.lightMarchStepM;
    float distAcc = 0.0f;
    [unroll] for (int j = 0; j < kCloudSunMarchSteps; ++j)
    {
        distAcc += stepLen;
        float3 sp = pos + toSun * distAcc;
        float sh = CloudHeightFraction(sp, gCloud);
        densitySum += SampleCloudDensityCheap(sp, sh, gCloud,
            gBaseShapeNoise, gWeatherMap, gSamplerLinearWrap) * stepLen;
        stepLen *= 2.0f;
    }

    // クランプ無しだと雲が密集した視線で tauSun が飽和し exp(-tauSun) が 0 に張り付いて
    // 暗部の階調が失われる。上限を切ると減衰したオクターブに光量が残り階調が生まれる
    float tauSun = min(densitySum * gCloud.densityScale, kCloudMaxSunOpticalDepth);
    float cosTheta = dot(rayDir, toSun);

    // 多重散乱の近似（Hillaire, Frostbite の N オクターブ法）。
    // 単一散乱のみだと雲の内部が真っ黒になる（実際の雲が白いのは多重散乱のため）。
    // オクターブごとに消散・寄与・位相の非対称度を減衰させた項を足し込む
    float3 energy = float3(0.0f, 0.0f, 0.0f);
    float attenuation = 1.0f;
    float contribution = 1.0f;
    float eccentricity = 1.0f;

    [unroll] for (int o = 0; o < kCloudMultiScatterOctaves; ++o)
    {
        // 二重ローブ Henyey-Greenstein 位相関数（前方散乱 + 弱い後方散乱）
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

    // 位相関数の 1/4π 正規化と、sunIntensity が放射照度スケールの美術値であることを吸収する
    energy *= gCloud.sunLightScale;

    // 大気透過率（夕暮れに雲が赤くなる要因）。大気座標系は km・惑星中心基準。
    // 高度差に加えて水平距離も含める。惑星中心基準では水平に離れた点ほど天頂方向が傾き、
    // 太陽の局所高度が方位で変わる。これが日没時の「太陽側の雲は照り、反太陽側は暗い」を生む
    float3 offsetKm = (pos - gCloud.cameraWorldPos) * 0.001f;
    float3 posAtmo = float3(offsetKm.x, gAtmosphere.cameraRadiusKm + offsetKm.y, offsetKm.z);
    float3 sunTrans = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler,
                                               posAtmo, toSun, gAtmosphere);

    return sunTrans * energy * lightColor * lightIntensity;
}

/// @brief 雲内部の 1 点における直接光（太陽＋月）の合計輝度
/// @note 月有効時のみ 2 本目のライトマーチが走る（コスト約 2 倍は夜間限定）
float3 CloudSunLuminance(float3 pos, float3 rayDir)
{
    float3 luminance = CloudDirectLightLuminance(pos, rayDir,
        gCloud.sunDirection, gCloud.sunColor, gCloud.sunIntensity);
    if (gCloud.hasMoon > 0.5f)
    {
        luminance += CloudDirectLightLuminance(pos, rayDir,
            gCloud.moonDirection, gCloud.moonColor, gCloud.moonIntensity);
    }
    return luminance;
}

/// @brief 雲内部の 1 点における空由来のアンビエント輝度
/// @param h 雲層内の高さ率（底ほど暗くする遮蔽近似）
/// @details Sky-View LUT のやや上向き 1 サンプルを半球平均の代用とする。
///          LUT はライト色・強度前乗算済みのため、サンプル後の色乗算はしない
float3 CloudAmbientLuminance(float h)
{
    float2 uv = SkyViewParamsToUv(false, kCloudAmbientCosZenith, CloudSkyViewAzimuth(),
                                  CloudSafeCameraRadiusKm(), gAtmosphere.planetRadiusKm);
    float3 skyLum = gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb;

    // 空色（青）のままだと太陽光が届かない厚い部分が青黒い染みに見える。
    // 実際の雲内部は多重散乱で無彩色化するため、大部分を灰色へ寄せる
    float gray = dot(skyLum, float3(0.333f, 0.333f, 0.334f));
    skyLum = lerp(float3(gray, gray, gray), skyLum, kCloudAmbientChroma);

    return skyLum * gCloud.ambientIntensity * lerp(kCloudAmbientBottomOcclusion, 1.0f, h);
}

#endif // CLOUD_LIGHTING_HLSLI

/// @file CloudLighting.hlsli
/// @brief 雲内部の 1 点における直接光とアンビエントの輝度
/// @details 輝度ドメインは空（SkyAtmosphere.PS）と共通。
///          セルフシャドウのサンライトマーチ + Beer-Powder + 二重ローブ HG 位相関数 +
///          Transmittance LUT による太陽色 + Sky-View LUT によるアンビエント。

#ifndef CLOUD_LIGHTING_HLSLI
#define CLOUD_LIGHTING_HLSLI

#include "CloudMarchBindings.hlsli"

/// @brief Draine 位相関数（HG に前方の鋭さを与える一般化）
/// @param g 非対称度
/// @param alpha 前方ピークの鋭さ
float DrainePhase(float g, float alpha, float cosTheta)
{
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    float hg = (1.0f - g2) / (4.0f * PI * denom * sqrt(max(denom, 1e-6f)));
    return hg * (1.0f + alpha * cosTheta * cosTheta) / (1.0f + alpha * (1.0f + 2.0f * g2) / 3.0f);
}

/// @brief 雲粒の Mie 位相関数の近似（Jendersie & d'Eon 2023）
/// @param diameterUm 粒径 [µm]。雲粒は 5〜50 の範囲
/// @details HG と Draine の重み付き和。粒径 1 つで前方の鋭いピーク・グローリー・
///          後方散乱の比が決まる。HG 2 ローブでは前方ピークが緩すぎて逆光の縁が光らない。
float CloudMiePhase(float diameterUm, float cosTheta)
{
    float d = clamp(diameterUm, 5.0f, 50.0f);
    float gHG = exp(-0.0990567f / (d - 1.67154f));
    float gD = exp(-(2.20679f / (d + 3.91029f)) - 0.428934f);
    float alpha = exp(3.62489f - 8.29288f / (d + 5.52825f));
    float wD = exp(-(0.599085f / (d - 0.641583f)) - 0.665888f);
    return lerp(HenyeyGreensteinPhase(gHG, cosTheta), DrainePhase(gD, alpha, cosTheta), wD);
}

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

    // 歩幅は「層を斜めに貫く経路長」から決める。絶対値 [m] で固定すると層厚と食い違い、
    // 後半のステップが層の外に出て必ず密度 0 を返す（サンプル数の空振り）
    float pathLen = min(gCloud.layerThicknessM / max(toSun.y, kCloudMinSunElevationSin),
                        gCloud.layerThicknessM * kCloudSunMaxPathMultiple);

    // 光源方向へ指数ステップでマーチし、光学的深さを積む
    float densitySum = 0.0f;
    float stepLen = pathLen * gCloud.lightMarchCoverage / kCloudSunMarchStepSum;
    float distAcc = 0.0f;
    [unroll] for (int j = 0; j < kCloudSunMarchSteps; ++j)
    {
        distAcc += stepLen;
        // 光源方向へ円錐状に散らし、視線上の 1 本の線だけでなく周囲の遮蔽も拾う。
        // 直線マーチだと自己影が板状になり、雲塊の丸みが出ない
        float3 sp = pos + toSun * distAcc
                  + kCloudSunConeKernel[j] * (distAcc * gCloud.lightMarchConeSpread);
        float sh = CloudHeightFraction(sp, gCloud);
        // ミップ段は最細固定。ここの歩幅は指数的に伸びるので、それを
        // フットプリントに使うと最上位ミップまで落ちて自己影が丸ごと消える
        densitySum += SampleCloudDensityCheap(sp, sh, 0.0f, gCloud,
            gBaseShapeNoise, gWeatherMap, gCloudPaintMap, gSamplerLinearWrap) * stepLen;
        stepLen *= kCloudSunMarchStepGrowth;
    }

    // クランプ無しだと雲が密集した視線で tauSun が飽和し exp(-tauSun) が 0 に張り付いて
    // 暗部の階調が失われる。上限を切ると減衰したオクターブに光量が残り階調が生まれる
    float tauSun = min(densitySum * gCloud.densityScale, gCloud.maxSunOpticalDepth);
    float cosTheta = dot(rayDir, toSun);

    // 多重散乱の近似（Hillaire, Frostbite の N オクターブ法）。
    // 単一散乱のみだと雲の内部が真っ黒になる（実際の雲が白いのは多重散乱のため）。
    // オクターブごとに消散・寄与・位相の非対称度を減衰させた項を足し込む
    // 前方ピークは太陽の視直径（0.53°）より鋭いので、半解像度では上限で丸めても差が出ない。
    // 丸めないと FP16 の雲バッファが逆光で飽和する
    float miePhase = min(CloudMiePhase(gCloud.dropletDiameterUm, cosTheta), gCloud.maxPhase);

    // Powder（雲の縁が暗く落ちる）は光源が観測者の背後にあるときだけ現れる現象。
    // cosTheta = dot(視線方向, 太陽方向) なので順光で -1、逆光で +1。
    // 角度を見ないと逆光でも縁が暗くなり、Mie の前方ピークで光るはずの縁を打ち消す
    float powderWeight = saturate(0.5f - 0.5f * cosTheta);

    float3 energy = float3(0.0f, 0.0f, 0.0f);
    float attenuation = 1.0f;
    float contribution = 1.0f;
    float eccentricity = 1.0f;

    [unroll] for (int o = 0; o < kCloudMultiScatterOctaves; ++o)
    {
        // オクターブが進むほど等方へ寄せる（多重散乱は方向性を失う）
        float phase = lerp(kCloudIsotropicPhase, miePhase, eccentricity);

        float tau = tauSun * attenuation;
        float beer = exp(-tau);
        float powder = 1.0f - exp(-tau * 2.0f);
        float lightEnergy = lerp(beer, beer * powder * 2.0f,
                                 gCloud.beerPowderStrength * 0.5f * powderWeight);

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

/// @brief Sky-View LUT を 1 方向サンプルし、彩度を落として返す
/// @param belowHorizon 地平線下（地表反射側）を引くか
/// @param cosZenith 天頂角の余弦
/// @details LUT はライト色・強度前乗算済みのため、サンプル後の色乗算はしない。
///          空色（青）のままだと太陽光が届かない厚い部分が青黒い染みに見える。
///          実際の雲内部は多重散乱で無彩色化するため、大部分を灰色へ寄せる
float3 CloudSampleAmbientDirection(bool belowHorizon, float cosZenith)
{
    float2 uv = SkyViewParamsToUv(belowHorizon, cosZenith, CloudSkyViewAzimuth(),
                                  CloudSafeCameraRadiusKm(), gAtmosphere.planetRadiusKm);
    float3 lum = gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb;
    float gray = dot(lum, float3(0.333f, 0.333f, 0.334f));
    return lerp(float3(gray, gray, gray), lum, gCloud.ambientChroma);
}

/// @brief 雲内部の 1 点における環境光（空 + 地表反射）の輝度
/// @param h 雲層内の高さ率（底ほど空が見えず地面が見える）
/// @details 上向き 1 サンプルを半球平均の代用とし、下向き 1 サンプルを地表反射とする。
///          Sky-View LUT の地平線下には地表アルベドの反射項が入っている。
float3 CloudAmbientLuminance(float h)
{
    float3 skyLum = CloudSampleAmbientDirection(false, gCloud.ambientCosZenith);
    float3 groundLum = CloudSampleAmbientDirection(true, -gCloud.ambientCosZenith);

    // 空は雲底ほど遮られる。地面光は下から入って上へ進むほど雲に消散されるので、
    // 到達範囲を高さのべき乗で絞る。線形に配ると層の中ほどまで明るくなり、
    // 雲塊の上下の陰影（立体感の主因）が消える
    float skyVisibility = lerp(gCloud.ambientBottomOcclusion, 1.0f, h);
    float groundReach = pow(saturate(1.0f - h), kCloudGroundReachPower);
    float3 ambient = skyLum * skyVisibility
                   + groundLum * groundReach * gCloud.ambientGroundStrength;

    return ambient * gCloud.ambientIntensity;
}

#endif // CLOUD_LIGHTING_HLSLI

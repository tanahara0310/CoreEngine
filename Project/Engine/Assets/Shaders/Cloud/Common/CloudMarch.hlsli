/// @file CloudMarch.hlsli
/// @brief 雲層をレイマーチしてエネルギー保存で積分する
/// @details CloudRayMarch.CS（メインビュー）と CloudCubemapCapture.CS（空キューブマップ）が共用する。
///          呼び出し側の違いはレイの作り方・反復予算・遮蔽距離の 3 点だけ。

#ifndef CLOUD_MARCH_HLSLI
#define CLOUD_MARCH_HLSLI

#include "CloudLighting.hlsli"

/// @brief マーチ結果（前乗算アルファ形式）
struct CloudMarchResult
{
    float3 luminance;     ///< 前乗算輝度
    float transmittance;  ///< 透過率（雲なし = 1）
    float distance;       ///< 不透明度で重み付けした雲の代表距離（雲なし = -1）
};

/// @brief 距離に応じたステップ伸長倍率
/// @details 遠方ほど画面上の 1 サンプルが覆う立体角が広がるためステップを伸ばす。
///          伸ばし量は必ず整数倍に量子化すること。連続倍率だとサンプル点がワールド固定格子から
///          外れ、カメラがわずかに動くだけで雲面が上下に振動する
uint CloudStrideAt(float t)
{
    return 1u + uint(
        kCloudStrideAmount0 * saturate((t - kCloudStrideStart0M) / kCloudStrideRange0M)
      + kCloudStrideAmount1 * saturate((t - kCloudStrideStart1M) / kCloudStrideRange1M)
      + kCloudStrideAmount2 * saturate((t - kCloudStrideStart2M) / kCloudStrideRange2M));
}

/// @brief 雲層をマーチして前乗算輝度と透過率を返す
/// @param rayOrigin レイ始点（ワールド [m]）
/// @param rayDir レイ方向（正規化）
/// @param marchStart マーチ開始距離
/// @param marchEnd マーチ終了距離
/// @param iterBudget 反復回数の上限
/// @param jitter サンプル位相のジッタ [0,1)
CloudMarchResult MarchClouds(float3 rayOrigin, float3 rayDir,
                             float marchStart, float marchEnd,
                             uint iterBudget, float jitter)
{
    CloudMarchResult result;
    result.luminance = float3(0.0f, 0.0f, 0.0f);
    result.transmittance = 1.0f;
    result.distance = -1.0f;

    if (marchStart >= marchEnd)
    {
        return result;
    }

    // ステップ幅は「雲層の厚み」を基準に決め、マーチ区間長には依存させない。
    // 区間長 / 固定ステップ数 にすると、カメラが動くたび dt が変化してサンプル位置が滑り、
    // 雲底の見かけの高さが上下に振動する
    float baseFine = max(gCloud.layerThicknessM / kCloudBaseStepDivisor, kCloudMinStepM);

    // サンプル格子を「ワールド固定の平面群」へアンカーする。
    // サンプル点 P = rayOrigin + rayDir * t の rayDir 方向への射影は
    //   P・rayDir = dot(rayOrigin, rayDir) + t
    // なので (dot(rayOrigin, rayDir) + t) を baseFine の格子に載せれば、
    // P は rayDir に垂直なワールド固定平面上に必ず乗る（カメラ移動で動かない）
    float proj = dot(rayOrigin, rayDir);
    float k = ceil((marchStart + proj) / baseFine - jitter);
    float tFirst = (k + jitter) * baseFine - proj;
    float t = tFirst;

    // 空気遠近用: 不透明度の寄与で重み付けした雲の平均距離
    float weightedDist = 0.0f;
    float weightSum = 0.0f;

    bool inCloud = false;  // 雲の中を細かくマーチ中か
    int emptyRun = 0;      // 雲の中で連続して密度 0 だった回数

    [loop] for (uint i = 0; i < iterBudget; ++i)
    {
        if (t >= marchEnd || result.transmittance < gCloud.earlyExitTransmittance)
        {
            break;
        }

        float dtFine = baseFine * float(CloudStrideAt(t));
        float dtBig = dtFine * kCloudCoarseStepScale;

        float3 pos = rayOrigin + rayDir * t;
        float hf = CloudHeightFraction(pos, gCloud);

        if (!inCloud)
        {
            // 空の空間は大股で走査する（ディテール無しの安価な密度で雲を探す）
            if (SampleCloudDensityCheap(pos, hf, dtBig, gCloud,
                    gBaseShapeNoise, gWeatherMap, gCloudPaintMap, gSamplerLinearWrap) > 0.0f)
            {
                // 雲を見つけた: 1 歩戻して細かいステップで入り直す。
                // dtBig は dtFine の整数倍なので、戻ってもワールド固定格子上に留まる
                inCloud = true;
                emptyRun = 0;
                t = max(t - dtBig, tFirst);
                continue;
            }
            t += dtBig;
            continue;
        }

        // 雲の中: 遠方はディテール侵食を弱めて高周波エイリアシングを防ぐ。
        // 早く消しすぎると中距離の雲が輪郭のないもや玉になる
        float detailFade = saturate(1.0f - (t - marchStart) / gCloud.detailFadeDistanceM);
        float density = SampleCloudDensity(pos, hf, dtFine, gCloud.detailErosionStrength * detailFade,
            gCloud, gBaseShapeNoise, gDetailNoise, gWeatherMap, gCloudPaintMap, gSamplerLinearWrap);

        // マーチ最大距離の手前でフェードし、層が地平線で唐突に切れないようにする
        density *= saturate((gCloud.maxMarchDistanceM - t) / gCloud.farFadeWidthM);

        if (density > 0.0f)
        {
            emptyRun = 0;

            // 雲はアルベド≒1（散乱係数 = 消散係数）
            float sigmaS = density * gCloud.densityScale;
            float sigmaE = max(sigmaS, 1e-7f);
            float stepTrans = exp(-sigmaE * dtFine);

            float3 luminance = CloudSunLuminance(pos, rayDir) + CloudAmbientLuminance(hf);
            float3 S = sigmaS * luminance;
            float3 Sint = (S - S * stepTrans) / sigmaE;

            result.luminance += result.transmittance * Sint;

            // このステップが最終的な不透明度へ寄与する量で距離を重み付けする
            float stepAlpha = result.transmittance * (1.0f - stepTrans);
            weightedDist += stepAlpha * t;
            weightSum += stepAlpha;

            result.transmittance *= stepTrans;
        }
        else if (++emptyRun > kCloudEmptyRunToExit)
        {
            // 雲を抜けた: 大股の走査へ戻す
            inCloud = false;
        }

        t += dtFine;
    }

    if (result.transmittance < gCloud.earlyExitTransmittance)
    {
        result.transmittance = 0.0f;
    }

    // 雲の代表距離。空気遠近と時間再投影の履歴 UV が使う
    if (1.0f - result.transmittance > 0.001f && weightSum > 1e-5f)
    {
        result.distance = weightedDist / weightSum;
    }

    return result;
}

// ===== 空気遠近（Aerial Perspective） =====
// 遠くの雲は大気の散乱で空の色へ溶け込む。これが無いと遠方の雲だけがくっきり浮いて
// 距離感が失われる。呼び出し側が引ける情報源に応じて 2 通りある。

/// @brief CameraVolume LUT で空気遠近を適用する
/// @param screenUv 画面 UV。LUT は視錐台のフロクセルなので画面座標で引く
/// @details 不透明ジオメトリの合成（AerialPerspective.CS）と同じ LUT を引くので、
///          雲と地形が地平線で同じ霞み方になる。LUT はライト色・強度前乗算済み。
void ApplyCloudAerialPerspective(inout CloudMarchResult cloud, float2 screenUv,
                                 Texture3D<float4> cameraVolumeLUT, SamplerState samp)
{
    if (cloud.distance <= 0.0f)
    {
        return;
    }

    float w = CameraVolumeDistanceToW(cloud.distance * 0.001f, gAtmosphere.apKmPerSlice);
    float4 aerial = cameraVolumeLUT.SampleLevel(samp, float3(screenUv, w), 0);

    // 前乗算輝度はカメラまでの透過率で減衰させ、内散乱は雲が覆う割合だけ足す
    // （覆っていない部分の空は SceneColor 側で内散乱を済ませている）
    float alpha = 1.0f - cloud.transmittance;
    cloud.luminance = cloud.luminance * aerial.a + aerial.rgb * alpha;
}

/// @brief 方向から引く空気遠近の近似（Sky-View LUT + 指数消散）
/// @details CameraVolume LUT は視錐台内しか持たないので、視錐台の外を焼く
///          キューブマップ側はこちらを使う。
void ApplyCloudAerialByDirection(inout CloudMarchResult cloud, float3 rayDir)
{
    if (cloud.distance <= 0.0f)
    {
        return;
    }

    float radiusKm = CloudSafeCameraRadiusKm();
    float cosHorizon = -sqrt(max(0.0f,
        1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
    float2 skyUv = SkyViewParamsToUv(rayDir.y < cosHorizon, rayDir.y, CloudSkyViewAzimuth(),
                                     radiusKm, gAtmosphere.planetRadiusKm);
    // LUT はライト色・強度前乗算済みのため、サンプル後の色乗算はしない
    float3 skyLum = gSkyViewLUT.SampleLevel(gLUTSampler, skyUv, 0).rgb;

    float alpha = 1.0f - cloud.transmittance;
    float haze = 1.0f - exp(-cloud.distance / gCloud.hazeDistanceM);
    cloud.luminance = lerp(cloud.luminance, skyLum * alpha, haze);
}

#endif // CLOUD_MARCH_HLSLI

/// @file CloudRayMarch.CS.hlsl
/// @brief 雲の半解像度レイマーチ（Phase 4: 物理ベースライティング）
/// @details 各ピクセル 1 レイ。球殻交差でマーチ区間を求め、SceneDepth で不透明遮蔽を
///          クランプし、密度をエネルギー保存で積分する。
///          ライティング: サンライトマーチによるセルフシャドウ + Beer-Powder +
///          二重ローブ HG 位相関数 + Transmittance LUT による太陽色 +
///          Sky-View LUT によるアンビエント。輝度ドメインは空（SkyAtmosphere.PS）と共通。
///          出力は前乗算アルファ形式（rgb=前乗算輝度, a=透過率）。

#include "Common/CloudCommon.hlsli"
#include "../Atmosphere/Common/AtmosphereCommon.hlsli"

ConstantBuffer<CloudConstants> gCloud : register(b0);
ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b1);
Texture3D<float4> gBaseShapeNoise : register(t0);
Texture3D<float4> gDetailNoise : register(t1);
Texture2D<float4> gWeatherMap : register(t2);
Texture2D<float>  gSceneDepth : register(t3);
Texture2D<float4> gTransmittanceLUT : register(t4);
Texture2D<float4> gSkyViewLUT : register(t5);
SamplerState gSamplerLinearWrap : register(s0);
RWTexture2D<float4> gCloudOutput : register(u0);

/// @brief 雲内部の 1 点における太陽由来の輝度
/// @param pos ワールド座標 [m]
/// @param rayDir 視線方向（位相関数用）
/// @details セルフシャドウのサンライトマーチは「指数的に伸びる固定ステップ」で行う
///          （Schneider/Nubis 標準）。近傍を密に・遠方を粗くサンプルするため、
///          ピクセルごとのジッタ無しでも滑らかな遮蔽が得られる。
///          以前は固定 200m×5 + ジッタで行っていたが、ジッタ（画面空間 IGN）が
///          exp() の急峻さで増幅され、IGN 特有の斜めドット格子が雲面に焼き付いていた。
float3 SunLuminanceAt(float3 pos, float3 rayDir)
{
    float3 toSun = -gCloud.sunDirection; // sunDirection は光の進行方向

    // (1) 雲自身によるセルフシャドウ: 太陽方向へ指数ステップでマーチし光学的深さを積む。
    //     累積距離 = 200, 600, 1400, 3000, 6200, 12600 m（近傍密・遠方粗）。
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

    // tauSun をクランプする（非物理的だが実用上必要な対策）。
    // クランプ無しだと雲が密集した方向（複数の雲塊が重なる視線）で tauSun が
    // 数十〜100 に達し exp(-tauSun) が完全に 0 へ飽和し、暗部の階調が失われる。
    // 8 程度でクランプすると oct1/oct2（消散が減衰したオクターブ）に低いながらも
    // 意味のある光量が残り、暗部にも階調が生まれる。
    float tauSun = min(densitySum * gCloud.densityScale, 8.0f);
    float cosTheta = dot(rayDir, toSun);

    // (2) 多重散乱の近似（Hillaire, Frostbite の N オクターブ法）
    // 単一散乱のみだと exp(-tauSun) が 0 に落ちて雲の内部が真っ黒になる
    // （実際の雲が白いのは多重散乱のため）。オクターブごとに消散・寄与・位相の
    // 非対称度を減衰させた項を足し込み、濃い部分にも光を残す。
    // 各オクターブで Beer 減衰 + Powder（雲の内側ほど明るく見える近似）を適用する。
    float3 energy = float3(0.0f, 0.0f, 0.0f);
    float attenuation = 1.0f;   // 消散のスケール
    float contribution = 1.0f;  // 寄与の重み
    float eccentricity = 1.0f;  // 位相関数の非対称度スケール

    [unroll] for (int o = 0; o < 3; ++o)
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

        // 消散の減衰を緩くしすぎる（0.25 等）と濃い雲の内部にも明るい「床」が残り、
        // 雲底が一様な白い板に見える。実際の積雲の底は灰色。
        attenuation *= gCloud.msAttenuation;
        contribution *= gCloud.msContribution;
        eccentricity *= gCloud.msEccentricity;
    }

    // 位相関数の 1/4π 正規化と、sunIntensity が放射照度スケールの美術値であることを
    // 吸収するスケール。ここを 4π（=12.57）にすると雲の HDR 値がトーンマッパーの
    // 線形域（空の輝度 ≈1.5）の 4〜13 倍まで飛び出し、明部も暗部も同じ白へ潰れて
    // 陰影が完全に失われる。空の輝度と同程度〜数倍に収まる値にすること。
    energy *= gCloud.sunLightScale;

    // (3) 大気透過率（夕暮れに雲が赤くなる要因）。大気座標系は km・惑星中心基準。
    //     雲の高度差のみを反映すれば十分。
    float radiusKm = gAtmosphere.cameraRadiusKm + (pos.y - gCloud.cameraWorldPos.y) * 0.001f;
    float3 posAtmo = float3(0.0f, radiusKm, 0.0f);
    float3 sunTrans = SampleTransmittanceToSun(gTransmittanceLUT, gSamplerLinearWrap,
                                               posAtmo, toSun, gAtmosphere);

    return sunTrans * energy * gCloud.sunColor * gCloud.sunIntensity;
}

/// @brief 雲内部の 1 点における空由来のアンビエント輝度
/// @param h 雲層内の高さ率（底ほど暗くする遮蔽近似）
float3 AmbientLuminance(float h)
{
    // Sky-View LUT のやや上向き 1 サンプルを半球平均の代用とする
    float radiusKm = gAtmosphere.cameraRadiusKm;
    float2 uv = SkyViewParamsToUv(false, 0.7f, 0.0f, radiusKm, gAtmosphere.planetRadiusKm);
    float3 skyLum = gSkyViewLUT.SampleLevel(gSamplerLinearWrap, uv, 0).rgb;

    // 空（SkyAtmosphere.PS）と同じ輝度ドメインへ揃える
    skyLum *= gCloud.sunColor * gCloud.sunIntensity;

    // 空色（青）をそのまま使うと、太陽光が届かない厚い部分が青黒い染みに見える。
    // 実際の雲内部は多重散乱で色が無彩色化するため、大部分を灰色へ寄せる。
    float gray = dot(skyLum, float3(0.333f, 0.333f, 0.334f));
    skyLum = lerp(float3(gray, gray, gray), skyLum, 0.35f);

    // 雲層の底ほど空からの光が遮られる（自己遮蔽の近似）
    return skyLum * gCloud.ambientIntensity * lerp(0.45f, 1.0f, h);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint w = gCloud.outputWidth;
    uint h = gCloud.outputHeight;
    if (dtid.x >= w || dtid.y >= h)
    {
        return;
    }

    // ===== レイ生成（半解像度ピクセル → NDC → ワールド方向） =====
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(w, h);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);

    float4 farH = mul(float4(ndc, 1.0f, 1.0f), gCloud.invViewProj);
    farH /= farH.w;
    float3 rayOrigin = gCloud.cameraWorldPos;
    float3 rayDir = normalize(farH.xyz - rayOrigin);

    // ===== マーチ区間（雲層シェル） =====
    float2 interval = CloudLayerInterval(rayOrigin, rayDir, gCloud);
    float marchStart = interval.x;
    float marchEnd = min(interval.y, gCloud.maxMarchDistanceM);

    // ===== 不透明ジオメトリによる遮蔽 =====
    // 出力が半解像度でも動くよう、深度テクスチャの実寸から対応画素を求める
    uint depthW, depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    int2 fullPix = int2((uv * float2(depthW, depthH)));
    fullPix = clamp(fullPix, int2(0, 0), int2(depthW - 1, depthH - 1));
    float ndcDepth = gSceneDepth.Load(int3(fullPix, 0));
    if (ndcDepth < 0.9999999f)
    {
        float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gCloud.invViewProj);
        wp /= wp.w;
        float distToOpaque = length(wp.xyz - rayOrigin);
        marchEnd = min(marchEnd, distToOpaque);
    }

    if (marchStart >= marchEnd)
    {
        gCloudOutput[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f); // 雲なし・透過率1
        return;
    }

    // ===== 積分（エネルギー保存・Frostbite 式） =====
    // ステップ幅は「雲層の厚み」を基準に決め、マーチ区間長には依存させない。
    // （区間長 / 固定ステップ数 にすると、カメラが動くたび dt が変化してサンプル位置が
    //   滑り、雲底の見かけの高さが上下に振動する。また地平線方向で dt が数百 m に
    //   膨らみ、厚さ数千 m の層を数サンプルしか貫かず「平らな板」に見えてしまう。）
    float baseFine = max(gCloud.layerThicknessM / 128.0f, 4.0f);
    uint maxIter = max(gCloud.maxSteps, 1u) * 2u;

    // 画面空間 IGN でサンプル位相をジッタし、等距離サンプル面が作る
    // 放射状のバンディング（規則的な縞・穴）をノイズへ分散させる。
    float ign = InterleavedGradientNoise(float2(dtid.xy));

    // サンプル格子を「ワールド固定の平面群」へアンカーする。
    // marchStart を起点にすると、カメラが上下するたび marchStart が変わって
    // 格子がレイに沿って滑り、雲の表面位置が量子化幅（数十 m）で前後する。
    // 雲は数ステップで不透明になるため、これが「雲が縦に振動する」正体になる。
    //
    // サンプル点 P = rayOrigin + rayDir * t の rayDir 方向への射影は
    //   P・rayDir = dot(rayOrigin, rayDir) + t
    // なので、(dot(rayOrigin, rayDir) + t) を baseFine の格子（+ジッタ）に載せれば、
    // P は rayDir に垂直なワールド固定平面上に必ず乗る（カメラ移動で動かない）。
    float proj = dot(rayOrigin, rayDir);
    float k = ceil((marchStart + proj) / baseFine - ign);
    float tFirst = (k + ign) * baseFine - proj;   // 格子上の最初のサンプル
    float t = tFirst;

    float3 color = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;

    // 空気遠近用: 不透明度の寄与で重み付けした雲の平均距離を積算する
    float weightedDist = 0.0f;
    float weightSum = 0.0f;

    bool inCloud = false;      // 雲の中を細かくマーチ中か
    int emptyRun = 0;          // 雲の中で連続して密度 0 だった回数

    [loop] for (uint i = 0; i < maxIter; ++i)
    {
        if (t >= marchEnd || transmittance < gCloud.earlyExitTransmittance)
        {
            break;
        }

        // 遠方ほどステップを伸ばす（画面上の 1 サンプルが覆う立体角が広がるため）。
        // ただし伸ばし量は baseFine の「整数倍」に量子化する。以前は連続値の倍率
        // （1.0〜8.0）を掛けていたため、4000m 以遠でサンプル点がワールド固定格子から
        // 外れ、カメラがわずかに動くたびにサンプル位置がレイに沿って滑り、雲面が
        // 上下に振動していた（DebugCamera のスムージングによる減衰微動が続く間、
        // 振動が徐々に収束するように見える）。整数倍ステップなら全サンプルが常に
        // ワールド固定平面群の上に乗り、カメラ移動でサンプル位置が動かない。
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
            // 空の空間は大股で走査する（ディテール無しの安価な密度で雲を探す）
            float probe = SampleCloudDensity(pos, hf, true, 0.0f, gCloud,
                gBaseShapeNoise, gDetailNoise, gWeatherMap, gSamplerLinearWrap);
            if (probe > 0.0f)
            {
                // 雲を見つけた: 1 歩戻して細かいステップで入り直す。
                // dtBig は dtFine の 4 倍なので、戻ってもワールド固定格子上に留まる。
                inCloud = true;
                emptyRun = 0;
                t = max(t - dtBig, tFirst);
                continue;
            }
            t += dtBig;
            continue;
        }

        // 雲の中: 遠方はディテール侵食を弱めて高周波エイリアシング（ちらつき）を防ぐ。
        // 早く消しすぎると中距離の雲が輪郭のないもや玉になるため、25km かけてフェードする。
        float detailFade = saturate(1.0f - distanceAlongRay / 25000.0f);
        float density = SampleCloudDensity(pos, hf, false,
            gCloud.detailErosionStrength * detailFade, gCloud,
            gBaseShapeNoise, gDetailNoise, gWeatherMap, gSamplerLinearWrap);

        // マーチ最大距離の手前でフェードし、層が地平線で唐突に切れないようにする
        density *= saturate((gCloud.maxMarchDistanceM - t) / 8000.0f);

        if (density > 0.0f)
        {
            emptyRun = 0;

            // 雲はアルベド≒1（散乱係数 = 消散係数）
            float sigmaS = density * gCloud.densityScale;
            float sigmaE = max(sigmaS, 1e-7f);
            float stepTrans = exp(-sigmaE * dtFine);

            float3 luminance = SunLuminanceAt(pos, rayDir) + AmbientLuminance(hf);
            float3 S = sigmaS * luminance;
            float3 Sint = (S - S * stepTrans) / sigmaE;

            color += transmittance * Sint;

            // このステップが最終的な不透明度へ寄与する量で距離を重み付けする
            float stepAlpha = transmittance * (1.0f - stepTrans);
            weightedDist += stepAlpha * t;
            weightSum += stepAlpha;

            transmittance *= stepTrans;
        }
        else if (++emptyRun > 8)
        {
            // 雲を抜けた: 大股の走査へ戻す
            inCloud = false;
        }

        t += dtFine;
    }

    if (transmittance < gCloud.earlyExitTransmittance)
    {
        transmittance = 0.0f;
    }

    // ===== 空気遠近（雲への Aerial Perspective 近似） =====
    // 遠くの雲は大気の散乱（ヘイズ）で空の色へ溶け込む。これが無いと遠方の雲だけが
    // くっきり浮いて見え、距離感（スケール感）が失われる。雲の平均距離に応じて
    // 前乗算輝度を「視線方向の空の輝度 × 雲の不透明度」へ寄せる。
    float alpha = 1.0f - transmittance;
    if (alpha > 0.001f && weightSum > 1e-5f)
    {
        float cloudDist = weightedDist / weightSum;

        // Sky-View LUT を視線方向の仰角のみでサンプルする。
        // 方位角（太陽との相対角 lightViewCos）は当初 dot(viewH/|viewH|, sunH/|sunH|) で
        // 毎ピクセル計算していたが、この値は SkyViewParamsToUv 内の sqrt エンコードで
        // U 座標へ変換される際、太陽の方位角（および真反対の方位角）＝画面上で太陽の
        // 軌跡（子午線）に一致する列で不安定になり、その列だけ薄暗い縦筋として見えて
        // いた。この処理は雲の不透明度がある画素でしか実行されない（alpha>0.001 の
        // ときだけ）ため、同じ LUT パラメータ化を使う空自体（SkyAtmosphere.PS.hlsl）
        // には出ず「雲にだけ太陽の軌跡上に線が入る」不具合として現れていた。
        // AmbientLuminance 関数と同様、方位角は固定値（太陽から 90°の向き）にして
        // 毎ピクセルの方位角計算自体を無くす。ヘイズは遠景をぼかす近似であり、
        // 方位角までの精度は不要（仰角による空の明るさ変化の方が支配的）。
        float radiusKm = clamp(gAtmosphere.cameraRadiusKm,
            gAtmosphere.planetRadiusKm + 0.001f,
            gAtmosphere.atmosphereTopRadiusKm - 0.001f);
        float cosHorizon = -sqrt(max(0.0f,
            1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
        float2 skyUv = SkyViewParamsToUv(rayDir.y < cosHorizon, rayDir.y, 0.0f,
                                         radiusKm, gAtmosphere.planetRadiusKm);
        float3 skyLum = gSkyViewLUT.SampleLevel(gSamplerLinearWrap, skyUv, 0).rgb
                      * gCloud.sunColor * gCloud.sunIntensity;

        // 60km で約 63% 空へ溶ける消散近似（地表付近の Rayleigh+Mie の視程感覚に合わせた美術値）
        float haze = 1.0f - exp(-cloudDist / 60000.0f);
        color = lerp(color, skyLum * alpha, haze);
    }

    gCloudOutput[dtid.xy] = float4(color, transmittance);
}

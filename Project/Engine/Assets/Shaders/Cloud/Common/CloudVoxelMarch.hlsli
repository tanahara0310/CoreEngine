/// @file CloudVoxelMarch.hlsli
/// @brief ブロック雲の DDA ボクセルトラバーサルと面フラットシェーディング
/// @details 固定ステップのレイマーチ（CloudMarch.hlsli）には、ブロック雲では致命的な
///          弱点が 2 つある。
///            1. 面の位置をステップ幅の粒度でしか決められない（縁が階段状に汚れる）
///            2. 雲探索の粗ステップ（最悪で層厚の半分）が薄い雲層を跨いで素通りし、
///               画素ごとに当たり外れが変わって雲が点滅する
///          DDA（Amanatides-Woo）はレイが横切るボクセルを順に漏れなく訪問するので、
///          どちらも構造的に起こらない。面の法線も「どの軸をまたいで入ったか」から
///          厳密に得られるため、体積散乱ではなく面ごとのフラットシェーディングができる。
/// @note 水平方向にも格子があるスタイル（Voxel / Minecraft）専用。Terraced は縦しか
///       量子化せず水平の輪郭が連続なので、たどるべき格子が無く固定ステップのままにする。
///       C++ 側の振り分けは CloudStyleUsesVoxelMarch()。

#ifndef CLOUD_VOXEL_MARCH_HLSLI
#define CLOUD_VOXEL_MARCH_HLSLI

#include "CloudMarch.hlsli"

// ===== 面のライティング =====

/// @brief ボクセル 1 面が 1 灯から受ける輝度
/// @param pos 面の代表位置（セル中心のワールド座標）
/// @param normal 面の外向き法線
/// @details 体積散乱ではなく面ごとのフラットシェーディング。面の向きは 6 通りしか無いので
///          量子化しなくてもマイクラ風の平坦な段差になる。
///          光源色と大気透過率は CloudLighting.hlsli と同じ引き方をするので、
///          夕暮れに雲が赤くなる・夜は月光になる、といった時刻追従はそのまま効く。
/// @note saturate(N·L) を使ってはいけない。光源に正対しない面が全て 0 へ潰れるため、
///       太陽が高いときは側面 4 枚と下面が同じ値になり、見上げた雲が一色の平面に見える。
///       ラップ拡散（-1..1 を 0..1 へ写す）にすると、背を向けた面まで含めて
///       向きごとに必ず違う値になり、太陽の位置への追従も保たれる。
float3 CloudVoxelFaceDirect(float3 pos, float3 normal,
                            float3 lightDirection, float3 lightColor, float lightIntensity)
{
    float3 toLight = -lightDirection;   // lightDirection は光の進行方向

    float wrapped = dot(normal, toLight) * 0.5f + 0.5f;
    float shade = lerp(gCloud.voxelFaceShadeMin, 1.0f, wrapped);

    // 大気透過率。大気座標系は km・惑星中心基準（CloudDirectLightLuminance と同じ変換）
    float3 offsetKm = (pos - gCloud.cameraWorldPos) * 0.001f;
    float3 posAtmo = float3(offsetKm.x, gAtmosphere.cameraRadiusKm + offsetKm.y, offsetKm.z);
    float3 lightTrans = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler,
                                                 posAtmo, toLight, gAtmosphere);

    return lightTrans * shade * lightColor * lightIntensity
         * gCloud.sunLightScale * gCloud.voxelFaceBrightness;
}

/// @brief ボクセル 1 面が受ける環境光
/// @details CloudAmbientLuminance は体積内の 1 点向けで法線を引数に取らないため、
///          そのまま使うと全ての面が同じ明るさになり、直接光の陰影まで薄まって
///          雲が一色の平面に見える。面の上向き具合で空（上）と地表反射（下）を配分し、
///          「上面が明るく下面が暗い」という立体感の土台をここで作る。
float3 CloudVoxelFaceAmbient(float3 pos, float3 normal)
{
    float3 up = normalize(pos - CloudPlanetCenter(gCloud));
    float upness = dot(normal, up) * 0.5f + 0.5f;   // 下向き 0 / 水平 0.5 / 上向き 1

    float3 skyLum = CloudSampleAmbientDirection(false, gCloud.ambientCosZenith);
    float3 groundLum = CloudSampleAmbientDirection(true, -gCloud.ambientCosZenith);

    // 下面は空が見えず地表反射だけを受ける。ambientBottomOcclusion をその下限に使う
    // （体積版で「雲底ほど空が遮られる」係数だったものを、面の向きへ読み替える）
    float skyVisibility = lerp(gCloud.ambientBottomOcclusion, 1.0f, upness);
    float groundReach = 1.0f - upness;

    return (skyLum * skyVisibility + groundLum * groundReach * gCloud.ambientGroundStrength)
         * gCloud.ambientIntensity;
}

/// @brief ボクセル 1 面の輝度（太陽 + 月 + 環境光）
float3 CloudVoxelFaceLuminance(float3 pos, float3 normal)
{
    float3 luminance = CloudVoxelFaceDirect(pos, normal,
        gCloud.sunDirection, gCloud.sunColor, gCloud.sunIntensity);
    if (gCloud.hasMoon > 0.5f)
    {
        luminance += CloudVoxelFaceDirect(pos, normal,
            gCloud.moonDirection, gCloud.moonColor, gCloud.moonIntensity);
    }
    return luminance + CloudVoxelFaceAmbient(pos, normal);
}

// ===== DDA =====

/// @brief 符号を保ったまま 0 除算を避けた方向ベクトル
/// @details 軸に平行な成分をそのまま割ると inf / NaN になる。下限を与えると
///          その軸の tDelta が実質無限大になり「永遠に境界をまたがない」= 正しい挙動になる。
float3 CloudVoxelSafeDir(float3 rayDir)
{
    float3 s = float3(rayDir.x < 0.0f ? -1.0f : 1.0f,
                      rayDir.y < 0.0f ? -1.0f : 1.0f,
                      rayDir.z < 0.0f ? -1.0f : 1.0f);
    return s * max(abs(rayDir), kCloudVoxelMinDirComponent);
}

/// @brief 3 成分のうち最小の軸番号
uint CloudVoxelMinAxis(float3 v)
{
    if (v.x <= v.y && v.x <= v.z)
    {
        return 0u;
    }
    return (v.y <= v.z) ? 1u : 2u;
}

/// @brief 軸番号と進行方向から、またいだ面の外向き法線を作る
float3 CloudVoxelFaceNormal(uint axis, float3 stepDir)
{
    float3 n = float3(0.0f, 0.0f, 0.0f);
    // 進行方向と逆を向く面から入る
    if (axis == 0u) { n.x = -stepDir.x; }
    else if (axis == 1u) { n.y = -stepDir.y; }
    else { n.z = -stepDir.z; }
    return n;
}

/// @brief 雲層をボクセル単位でたどって前乗算輝度と透過率を返す
/// @param rayOrigin レイ始点（ワールド [m]）
/// @param rayDir レイ方向（正規化）
/// @param marchStart 雲層シェルへの入射距離
/// @param marchEnd マーチ終了距離
/// @param iterBudget 訪問セル数の上限
CloudMarchResult MarchCloudVoxels(float3 rayOrigin, float3 rayDir,
                                  float marchStart, float marchEnd, uint iterBudget)
{
    CloudMarchResult result;
    result.luminance = float3(0.0f, 0.0f, 0.0f);
    result.transmittance = 1.0f;
    result.distance = -1.0f;

    if (marchStart >= marchEnd)
    {
        return result;
    }

    // 格子は量子化（CloudBlocky.hlsli）と同じ「風とともに動く座標系」で切る。
    // ここがずれると、DDA がたどるセル境界と密度関数が返す値の境界が食い違い、
    // 面が割れたり 1 セルぶんずれた影が出る
    const float3 cell = CloudBlockyCellSize(gCloud);
    const float3 windOffset = CloudBlockyWindOffset(gCloud);
    const float3 origin = rayOrigin + windOffset;

    const float3 safeDir = CloudVoxelSafeDir(rayDir);
    const float3 stepDir = sign(safeDir);

    // 予算（訪問セル数）で届く距離。DDA は 1 反復 1 セルなので、ボクセルを小さくすると
    // 同じ予算で届く距離が比例して縮む。ここを見ずに打ち切ると遠方の雲が唐突に消えるため、
    // 密度フェードの基準もこの距離へ合わせて滑らかに消す。
    // @note ここを視線の向きごとに算出してはいけない。1m あたりの境界またぎ回数は
    //       |方向| の L1 ノルム / セル幅で、軸沿い 1 〜 体対角 √3 と 1.7 倍変わる。
    //       方向ごとの値を使うと終端が 68km〜118km も振れ、FarFadeWidth（既定 8km）では
    //       到底吸収できず、カメラを回すたびに遠方の雲の帯が湧いたり消えたりする。
    //       最も不利な向きで揃えれば、どのレイも必ずこの距離まで到達できる。
    const float minCell = min(cell.x, min(cell.y, cell.z));
    const float reachM = marchStart + float(iterBudget) * minCell / kCloudVoxelMaxL1;
    const float fadeEndM = min(gCloud.maxMarchDistanceM, reachM);
    marchEnd = min(marchEnd, reachM);

    float3 p = origin + rayDir * marchStart;
    int3 cellIndex = int3(floor(p / cell));

    // 各軸で次の境界に届くまでの t と、1 セル進むのに要する t
    const float3 tDelta = cell / abs(safeDir);
    float3 nextPlane = (float3(cellIndex) + max(stepDir, 0.0f)) * cell;
    float3 tMax = marchStart + (nextPlane - p) / safeDir;

    // 開始セルへは面をまたいで入っていないので、法線を別途決める。
    // シェルの外から入ったなら、またいだのは雲層の上面／下面なので法線は動径方向。
    // 層の中から始まったなら、直前にまたいだはずの面（後退距離が最小の軸）を逆算する
    float3 normal;
    {
        float3 up = normalize(rayOrigin - CloudPlanetCenter(gCloud));
        float h0 = CloudHeightFraction(rayOrigin, gCloud);
        if (h0 < 0.0f || h0 > 1.0f)
        {
            normal = (dot(rayDir, up) < 0.0f) ? up : -up;
        }
        else
        {
            float3 entryPlane = (float3(cellIndex) + max(-stepDir, 0.0f)) * cell;
            normal = CloudVoxelFaceNormal(CloudVoxelMinAxis((p - entryPlane) / safeDir), stepDir);
        }
    }

    // ブロック 1 個の不透明度。レイがそのセルを通過した長さでは決めない。
    // 通過長で決めると、セルの角や辺をかすめた画素だけ不透明度が落ちて、
    //   ・平らな面に格子状の筋（キューブの境目）が出る
    //   ・カメラが少し動くだけでブロックが濃くなったり薄くなったりする
    // という 2 つの症状が同時に出る。ブロックは面で構成された不透明体なので、
    // セル 1 個ぶんの厚みで固定するのが正しい
    const float cellAlpha = saturate(1.0f
        - exp(-max(gCloud.densityScale, 0.0f) * min(cell.x, min(cell.y, cell.z))));

    float t = marchStart;
    float weightedDist = 0.0f;
    float weightSum = 0.0f;

    // 塊へ入った瞬間の面が「見えている面」。塊の内部でセルをまたいで法線が変わっても
    // 色は変えない。変えると 1 つの平面の中で上面の色と側面の色が混ざり、
    // セル境界に沿って筋が出る
    bool inSolid = false;
    float3 runNormal = normal;

    [loop] for (uint i = 0; i < iterBudget; ++i)
    {
        if (t >= marchEnd || result.transmittance < gCloud.earlyExitTransmittance)
        {
            break;
        }

        // このセルを抜ける距離。3 軸のうち最も手前の境界で決まる
        const uint exitAxis = CloudVoxelMinAxis(tMax);
        const float tExit = min(min(tMax.x, min(tMax.y, tMax.z)), marchEnd);

        // セル中心で密度を引く。CloudQuantizeSample はセル中心を与えると同じセルへ
        // 丸め直す（冪等）ので、DDA がたどる格子と密度の格子は必ず一致する
        const float3 centerWS = (float3(cellIndex) + 0.5f) * cell - windOffset;
        const float hf = CloudHeightFraction(centerWS, gCloud);
        float density = SampleCloudDensityCheap(centerWS, hf, 0.0f, gCloud,
            gBaseShapeNoise, gWeatherMap, gCloudPaintMap, gSamplerLinearWrap);

        // 到達距離の手前でフェードし、層が地平線で唐突に切れないようにする
        density *= saturate((fadeEndM - t) / gCloud.farFadeWidthM);

        if (density > 0.0f)
        {
            if (!inSolid)
            {
                inSolid = true;
                runNormal = normal;
            }

            // density は 0/1 に遠方フェードを掛けたもの。フェードぶんだけ薄くする
            const float alpha = saturate(cellAlpha * density);
            const float3 luminance = CloudVoxelFaceLuminance(centerWS, runNormal);

            // 前乗算アルファの front-to-back 合成
            const float stepAlpha = result.transmittance * alpha;
            result.luminance += luminance * stepAlpha;
            weightedDist += stepAlpha * t;
            weightSum += stepAlpha;

            result.transmittance *= (1.0f - alpha);
        }
        else
        {
            inSolid = false;
        }

        // 次のセルへ。またいだ軸が次のセルの入射面になる
        t = tExit;
        normal = CloudVoxelFaceNormal(exitAxis, stepDir);
        if (exitAxis == 0u)      { cellIndex.x += int(stepDir.x); tMax.x += tDelta.x; }
        else if (exitAxis == 1u) { cellIndex.y += int(stepDir.y); tMax.y += tDelta.y; }
        else                     { cellIndex.z += int(stepDir.z); tMax.z += tDelta.z; }
    }

    if (result.transmittance < gCloud.earlyExitTransmittance)
    {
        result.transmittance = 0.0f;
    }

    // 雲の代表距離。空気遠近が使う
    if (1.0f - result.transmittance > 0.001f && weightSum > 1e-5f)
    {
        result.distance = weightedDist / weightSum;
    }

    return result;
}

#endif // CLOUD_VOXEL_MARCH_HLSLI

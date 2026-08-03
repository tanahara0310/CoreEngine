// ============================================================
// 水の体積色（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 天空光 SH 評価・水中インスキャッタ環境光・Beer-Lambert＋単一散乱の体積色・
// RT 屈折の透過色解決。水柱厚さ（WaterColumn.hlsli）と対で使う。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterFrameConstants(b5) の
// 後で include すること。以下に暗黙依存する:
//   資源    : gWaterSkyIrradianceSH / gIrradianceMap / gSampler / gSceneColor /
//             gLinearClamp / gRTWaterRefractionColor / gLightCounts / gDirectionalLights /
//             gIBLParams（Object3dForward.hlsli）
//   cbuffer : gSkyAmbientEnabled / gSkyAmbientScale
//   関数    : IsRTPathValid（Common/WaterRefractionEncoding.hlsli）
// ============================================================
#ifndef WATER_VOLUME_INCLUDED
#define WATER_VOLUME_INCLUDED

/// @brief SH9 係数から法線方向の空の放射照度/π を評価する
/// @details DeferredLighting.PS.hlsl の EvaluateSkyIrradiance と同じ評価式。
///          係数（gWaterSkyIrradianceSH）には放射照度畳み込みと太陽色・強度が焼き込み済み。
float3 EvaluateWaterSkyIrradiance(float3 n)
{
    float basis[9];
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        irradiance += gWaterSkyIrradianceSH[i].rgb * basis[i];
    }
    // SH の帯域打ち切りによる負のリンギングを防ぐ
    return max(irradiance, 0.0f);
}

/// @brief 水中インスキャッタリングの環境光（水面に入射する光の放射輝度近似）を求める
/// @details 平行光源（太陽）の下向き放射照度と天空拡散光を合算する。
///          太陽ライトの GPU 転送色には LightManager が大気の Transmittance LUT による
///          減衰（日没の赤方偏移・減光）を乗算済みのため、ここで読むだけで大気に追従する。
///          天空光は大気アクティブ時は Sky Irradiance SH（時刻・太陽高度に連動）、
///          非アクティブ時は静的な拡散 IBL キューブマップへフォールバックする。
float3 ComputeUnderwaterAmbientLight()
{

    // 太陽など平行光源: 水平な水面へ入る下向き放射照度を Lambert 面の放射輝度へ換算（/π）
    // downwelling は太陽高度に対してほぼ線形に 0 へ落ちる（sin(elevation) 相当）ため、
    // 太陽を下げていくと直射成分だけが急激に暗転する。
    float3 sunAmbient = float3(0.0f, 0.0f, 0.0f);
    float maxDownwelling = 0.0f;
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float downwelling = saturate(-normalize(gDirectionalLights[i].direction).y);
        maxDownwelling = max(maxDownwelling, downwelling);
        sunAmbient += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * downwelling / PI;
    }

    // 天空拡散光
    float3 skyAmbient = float3(0.0f, 0.0f, 0.0f);
    if (gSkyAmbientEnabled != 0)
    {
        // 大気散乱の空を SH9 で評価（上向き法線＝水面へ降り注ぐ天空放射照度/π）。
        // DeferredLighting と同じスケールでサーフェス光単位へ変換する
        skyAmbient = EvaluateWaterSkyIrradiance(float3(0.0f, 1.0f, 0.0f)) * gSkyAmbientScale;
    }
    else if (gIBLParams.sceneIBLEnabled != 0)
    {
        // Irradiance マップは既に「albedo に掛けるだけ」の放射輝度相当なのでそのまま加算
        skyAmbient = gIrradianceMap.SampleLevel(gSampler, float3(0.0f, 1.0f, 0.0f), 0.0f).rgb
                 * gIBLParams.environmentIntensity;
    }

    // 天空光はブーストせずそのまま使う。
    // 以前ここに「太陽が低いほど天空光を最大2.5倍へ底上げする」ハックがあったが、
    // 夜間に水中インスキャッタ（透過側）だけが空の反射（ほぼ黒）より明るくなり、
    // フレネルの振れに応じて青/黒のうねりスケールのまだらを生む原因になったため撤去。
    // 水中に入る光と水面に映る空は同じ空なので、両者の明るさは常に整合させる。
    return sunAmbient + skyAmbient;
}

// ★太陽の下り光路（太陽→海底）の吸収はこのシェーダーの責務ではない★（2026-07-27 撤去）
// DeferredLighting の水中ライティング置換（RT コースティクス）が、海底ピクセルの
// 直接光として exp(-σa·実光路長) を含む透過直接光を既に合成している。
// ここで再度掛けると下り光路が二重計上になるため、以前あった
// ComputeSunDownwellingTransmittance() は呼び出しごと削除した。
// 本シェーダーが担当するのは「視線の上り光路」（transmittance）のみ。

/// @brief 水柱を通過した背景色に波長依存の吸収・散乱を適用する
/// @param refractionColor 水面越しに見える背景（海底・水中物体）の色
/// @param transmittance   exp(-σt·d) 視線（上り）光路の波長別透過率
/// @param sigmaS          散乱係数 σs [1/m]
/// @param sigmaT          消散係数 σt = σa + σs [1/m]
/// @param ambientLight    水面に入射する環境光（ComputeUnderwaterAmbientLight）
/// @details 透過項 + 均質媒質の単一散乱解析解。
///          浅瀬では transmittance がまだ緑・青を通すため海底アルベド（白砂）が
///          エメラルドに、深瀬では透過が消えて (σs/σt)·L の水固有の青に収束する。
///          太陽 → 海底の下り光路の減衰は DeferredLighting の水中ライティング置換
///          （RT コースティクス）が海底色に織り込み済みなので、ここでは掛けない
///          （掛けると二重計上。上のコメント参照）。
float3 ComputeWaterVolumetricColor(
    float3 refractionColor, float3 transmittance,
    float3 sigmaS, float3 sigmaT, float3 ambientLight)
{
    float3 transmitted = refractionColor * transmittance;
    float3 inscatter = (sigmaS / sigmaT) * ambientLight * (1.0f - transmittance);
    return transmitted + inscatter;
}

// アルファのエンコード規約・IsRTPathValid / IsRTColorValid / DecodeRTOpticalPath は
// Common/WaterRefractionEncoding.hlsli（RTWaterRefraction.hlsl と共有）が唯一の情報源。
// 「光路長が有効か」と「色が有効か」を分けているのが要点。色が取れないだけの
// ピクセルでも光路長は正しいので、水柱厚さの推定を別の量へ切り替えてはいけない
// （切り替えると境界が透過率の段差＝波打ち際の二重線になる）。

float3 ResolveWaterTransmissionColor(uint2 pixelCoord, float2 screenUV)
{
    float4 rtRefraction = gRTWaterRefractionColor.Load(int3(pixelCoord, 0));

    // ★ここで IsRTColorValid の 2 値で切り替えてはいけない★
    // RTWaterRefraction.hlsl は「屈折先のシーン色」と「屈折させていない自分の
    // ピクセルのシーン色（フォールバック）」を depthConfidence × edgeFade で
    // 連続ブレンドした結果を rgb に書いている。ここで 2 値切替を入れると、
    // 岸際に帯状に出る色フォールバック領域の縁が色の段差になり、
    // 波打ち際に沿った細い暗線として見える（光路長で起きたのと同じ事故）。
    //
    // ヒットしていないピクセルの rgb もフォールバック色（＝この関数の else と
    // 同じ値）なので、実質どちらでも同じだが、RT パスが走っていないフレーム
    // （テクスチャがダミー）のために else 側は残す。
    if (IsRTPathValid(rtRefraction.a) > 0.5f) {
        return rtRefraction.rgb;
    }

    return gSceneColor.Sample(gLinearClamp, screenUV).rgb;
}

#endif // WATER_VOLUME_INCLUDED

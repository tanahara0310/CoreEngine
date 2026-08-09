/// @file SkyAtmosphere.PS.hlsl
/// @brief 大気散乱による空の描画（Phase 4: Sky-View LUT サンプリング）
/// @details SkyBox メッシュ（Skybox.VS.hlsl）の texcoord を視線方向として使用する。
///          レイマーチングは Sky-View LUT 生成時（SkyViewLUT.CS.hlsl）のみ行い、
///          本番描画は LUT 1サンプルで完結する。

#include "Common/AtmosphereCommon.hlsli"
#include "ColorSpace.hlsli" // Luminance

// Skybox.VS.hlsl の出力と一致させる（Skybox.hlsli と同一レイアウト）
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gSkyViewLUT : register(t0);
Texture2D<float4> gTransmittanceLUT : register(t1);
SamplerState gLUTSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float3 viewDir = normalize(input.texcoord);
    float3 toSun = -gAtmosphere.sunDirection;

    float radiusKm = clamp(gAtmosphere.cameraRadiusKm,
        gAtmosphere.planetRadiusKm + 0.001f,
        gAtmosphere.atmosphereTopRadiusKm - 0.001f);

    // 視線天頂角余弦（ワールド +Y = 天頂）と絶対方位角
    float viewZenithCos = viewDir.y;
    float viewAzimuth = SkyViewAzimuth(viewDir);

    // 視線が地面（仮想惑星）と交差するか
    float cosHorizon = -sqrt(max(0.0f,
        1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
    bool intersectGround = (viewZenithCos < cosHorizon);

    float2 uv = SkyViewParamsToUv(intersectGround, viewZenithCos, viewAzimuth,
                                  radiusKm, gAtmosphere.planetRadiusKm);

    float3 luminance = gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb;

    // ===== 太陽ディスク（解析的描画） =====
    // LUT 解像度に依存せず、視線と太陽方向の角度から直接円盤を描く。
    // 大気透過率を掛けることで夕暮れの赤い太陽・地平線下への自然な消失を再現する。
    if (!intersectGround)
    {
        // 視線と太陽の間の角度 [rad]
        float cosViewSun = clamp(dot(viewDir, toSun), -1.0f, 1.0f);
        float angleToSun = acos(cosViewSun);
        float halfAngle = max(gAtmosphere.sunDiskHalfAngleRad, 1e-5f);

        float3 cameraPos = float3(0.0f, radiusKm, 0.0f);
        // SampleTransmittanceToSun は惑星による遮蔽を含むため、太陽が地平線を
        // 跨ぐ間にディスクとグレアが一緒に滑らかに消える。
        float3 sunTransmittance = SampleTransmittanceToSun(
            gTransmittanceLUT, gLUTSampler, cameraPos, toSun, gAtmosphere);
        // LUT（空）はライト色前乗算済みのため、解析的に描くディスクにも同じ色・強度を掛けて
        // 輝度ドメインを揃える
        float3 sunRadiance = sunTransmittance * gAtmosphere.sunDiskLuminanceScale
                           * gAtmosphere.sunColor * gAtmosphere.sunIntensity;

        // 太陽本体: 縁を fwidth 1 ピクセル分だけソフトにして硬い輪郭（ジャギ）を防ぐ。
        // 角度基準の一定幅なので LUT／画面解像度に依存しない。
        float edge = max(fwidth(angleToSun), 1e-5f);
        float diskMask = 1.0f - smoothstep(halfAngle - edge, halfAngle + edge, angleToSun);

        // 周縁減光（limb darkening）: 太陽面は中心が明るく縁ほど暗い。縁で 0 へ落ちるため
        // ディスクの外周が「切り立った円」ではなくなる。指数は波長依存（青ほど強く暗い）。
        float centerToEdge = saturate(angleToSun / halfAngle);
        float muDisk = sqrt(saturate(1.0f - centerToEdge * centerToEdge));
        float3 limbDarkening = pow(max(muDisk, 1e-4f), float3(0.397f, 0.503f, 0.652f));

        // グレア（本体外側のにじみ）: 二重ローブの指数減衰。
        // 振幅は sunRadiance に「一定比率」で連動させる。以前は透過率を絶対値でクランプして
        // 振幅を抑えていたため、太陽が低いほど（＝透過率が小さいほど）にじみだけが先に
        // 見えなくなり、白飛びしたディスクだけが裸の完全な円として残っていた。
        // 比率を固定すればディスクとグレアの明暗差が高度によらず一定になり、
        // どの高度でも「にじんだ光球」として見える。
        // 見かけサイズの膨張は減衰幅で抑える（単位は太陽半径）: 内側 0.45 半径・外側 2.2 半径。
        // 可視半径は振幅の対数でしか伸びないので、振幅が 20 倍変わっても数半径しか広がらない。
        // なお太陽から数度に及ぶ広いオーラは Sky-View LUT のミー前方散乱が担う。
        float x = max(angleToSun - halfAngle, 0.0f) / halfAngle; // 太陽半径単位
        float3 glare = sunRadiance * (0.28f * exp(-x * 2.2f) + 0.02f * exp(-x * 0.45f));

        luminance += diskMask * sunRadiance * limbDarkening + glare;

        // ===== 月ディスク（第2大気ライト） =====
        if (gAtmosphere.hasMoon > 0.5f)
        {
            float3 toMoon = -gAtmosphere.moonDirection;
            float cosViewMoon = clamp(dot(viewDir, toMoon), -1.0f, 1.0f);
            float angleToMoon = acos(cosViewMoon);
            float moonHalfAngle = max(gAtmosphere.moonDiskHalfAngleRad, 1e-5f);

            // 透過率×惑星遮蔽で月の出・月の入りの減光と消失を太陽ディスクと同様に扱う
            float3 moonTransmittance = SampleTransmittanceToSun(
                gTransmittanceLUT, gLUTSampler, cameraPos, toMoon, gAtmosphere);
            float3 moonRadiance = moonTransmittance * gAtmosphere.moonDiskLuminanceScale
                                * gAtmosphere.moonColor * gAtmosphere.moonIntensity;

            float moonEdge = max(fwidth(angleToMoon), 1e-5f);
            float moonMask = 1.0f - smoothstep(moonHalfAngle - moonEdge, moonHalfAngle + moonEdge, angleToMoon);

            if (moonMask > 0.0f)
            {
                // ===== 満ち欠け =====
                // 既定は常に満月（moonPhaseFromSun=0）。ゲームでは太陽と月を独立に動かすため、
                // 実位相だと配置次第で意図せず新月（ほぼ見えない月）になる。
                // 太陽連動（=1）のときのみ、ディスク内ローカル座標から月面の球法線を復元し
                // 太陽方向のランバート陰影で新月〜満月を自動再現する
                // （周縁減光は掛けない: 月は反射体で、太陽と違い縁が暗くならない）。
                float moonPhase = 1.0f;
                if (gAtmosphere.moonPhaseFromSun > 0.5f)
                {
                    float r = saturate(angleToMoon / moonHalfAngle);      // ディスク中心 0 → 縁 1
                    float3 discU = viewDir - toMoon * cosViewMoon;        // 画面内の径方向
                    float discULen = length(discU);
                    discU = (discULen > 1e-6f) ? discU / discULen : float3(0.0f, 0.0f, 0.0f);
                    // 遠方球の可視面法線 ≒ 径方向成分 + 観測者方向（月→カメラ = -toMoon）成分
                    float3 moonNormal = normalize(discU * r - toMoon * sqrt(saturate(1.0f - r * r)));
                    moonPhase = saturate(dot(moonNormal, toSun));
                    // 地球照（新月側がわずかに見える現実の見え方）ぶんの床を足す
                    moonPhase = max(moonPhase, 0.02f);
                }

                luminance += moonMask * moonRadiance * moonPhase;
            }

            // 控えめなグレア（空の散乱に埋もれない程度のにじみ。太陽の係数より大幅に弱い）
            float xm = max(angleToMoon - moonHalfAngle, 0.0f) / moonHalfAngle;
            luminance += moonRadiance * 0.03f * exp(-xm * 1.8f);
        }

        // ===== 星空（Phase 5: 手続き的星field） =====
        // UE 同様、大気の「外側」の背景として合成する。
        // ここまでの輝度（散乱＋ディスク＋グレア）による暗さマスクを掛けるため、
        // 昼・薄明・月グレア近傍では判定コード無しで自然に星が消える。
        // 太陽・月ディスクの上でもマスクが 0 になるため、ディスクを星が貫通しない。
        if (gAtmosphere.starIntensity > 0.0f)
        {
            float pixelAngle = length(fwidth(viewDir));
            float3 star = ComputeStarLuminance(viewDir, pixelAngle, gAtmosphere.starIntensity);
            if (any(star > 0.0f))
            {
                // 視線方向に大気圏上端まで抜ける透過率（地平線際の減光・赤化。
                // 太陽用の Transmittance LUT は (r, 天頂角余弦) パラメータ化なのでそのまま使える）
                float2 viewTransUv = TransmittanceParamsToUv(radiusKm, viewZenithCos,
                    gAtmosphere.planetRadiusKm, gAtmosphere.atmosphereTopRadiusKm);
                float3 viewTransmittance = gTransmittanceLUT.SampleLevel(gLUTSampler, viewTransUv, 0).rgb;

                // 空の暗さマスク: 空の輝度がしきい値（月夜の空 ≈ 数e-3 は通し、
                // 薄明 ≈ 数e-2 以上で消える美術値）へ近づくほど星を消す
                const float kStarVisibilityLuminance = 0.02f;
                float skyLuma = Luminance(luminance);
                float darknessMask = saturate(1.0f - skyLuma / kStarVisibilityLuminance);

                luminance += star * viewTransmittance * darknessMask;
            }
        }
    }

    // Sky-View LUT はライト色・強度前乗算済み（ディスクも上で乗算済み）のためそのまま出力する。
    //
    // 地平線下（intersectGround）もここで完結する。以前はこの後に「無限遠タイル床の
    // 遠景埋め」として y=groundLevelY 平面との解析交差＋市松チェッカーを描き足していたが、
    // これは Sky-View LUT が地表の反射を積分していなかった（＝地平線下が真っ黒だった）ための
    // 対症療法だった。現在は IntegrateScatteredLuminance が地表のランバート反射を含むため、
    // 地平線下も物理的に照らされた地面として LUT から出てくる。実効スケールは
    // sunIntensity(既定20)/PI ≒ 6.4 で、旧フィルの美術値 kGroundLightScale=5.0 とほぼ一致する。
    output.color.rgb = luminance;

    output.color.a = 1.0f;
    return output;
}

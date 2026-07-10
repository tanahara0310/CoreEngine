/// @file SkyAtmosphere.PS.hlsl
/// @brief 大気散乱による空の描画（Phase 4: Sky-View LUT サンプリング）
/// @details SkyBox メッシュ（Skybox.VS.hlsl）の texcoord を視線方向として使用する。
///          レイマーチングは Sky-View LUT 生成時（SkyViewLUT.CS.hlsl）のみ行い、
///          本番描画は LUT 1サンプルで完結する。

#include "Common/AtmosphereCommon.hlsli"

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

    // 視線天頂角余弦（ワールド +Y = 天頂）
    float viewZenithCos = viewDir.y;

    // 太陽との相対方位角余弦（水平面内）
    float2 viewHorizontal = viewDir.xz;
    float2 sunHorizontal = toSun.xz;
    float viewHorizontalLen = length(viewHorizontal);
    float sunHorizontalLen = length(sunHorizontal);
    float lightViewCos = 1.0f;
    if (viewHorizontalLen > 1e-5f && sunHorizontalLen > 1e-5f)
    {
        lightViewCos = dot(viewHorizontal / viewHorizontalLen, sunHorizontal / sunHorizontalLen);
    }

    // 視線が地面（仮想惑星）と交差するか
    float cosHorizon = -sqrt(max(0.0f,
        1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
    bool intersectGround = (viewZenithCos < cosHorizon);

    float2 uv = SkyViewParamsToUv(intersectGround, viewZenithCos, lightViewCos,
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
        float3 sunRadiance = sunTransmittance * gAtmosphere.sunDiskLuminanceScale;

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
    }

    output.color.rgb = luminance * gAtmosphere.sunColor * gAtmosphere.sunIntensity;
    output.color.a = 1.0f;
    return output;
}

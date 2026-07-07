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
        float halfAngle = gAtmosphere.sunDiskHalfAngleRad;

        float3 cameraPos = float3(0.0f, radiusKm, 0.0f);
        float3 sunTransmittance = SampleTransmittanceToSun(
            gTransmittanceLUT, gLUTSampler, cameraPos, toSun, gAtmosphere);
        float3 sunRadiance = sunTransmittance * gAtmosphere.sunDiskLuminanceScale;

        // 太陽本体: 縁を fwidth 1 ピクセル分だけソフトにして硬い輪郭（ジャギ）を防ぐ。
        // 角度基準の一定幅なので LUT／画面解像度に依存しない。
        float edge = max(fwidth(angleToSun), 1e-5f);
        float disk = 1.0f - smoothstep(halfAngle - edge, halfAngle + edge, angleToSun);

        // 太陽周辺光（aureole）: 本体の外側に滑らかに減衰するにじみを加え、円盤が
        // 空へ唐突に切り立つ（クッキリした丸に見える）のを防ぐ。
        // 注意: にじみの振幅を sunRadiance（キャップ無し）にそのまま比例させると、
        // 天頂付近で透過率が最大になった瞬間に振幅が跳ね上がり、指数減衰の裾野が
        // トーンマップ後も「見える明るさ」として残る範囲＝見た目の太陽サイズが
        // 太陽の高度（明るさ）につれて際限なく膨らんでしまう。
        // そのため、にじみの振幅だけは透過率を小さい値でクランプしてから使い、
        // 本体(disk)の明るさ（＝白飛びの度合い）と、にじみの「広がり」を分離する。
        float glowAngle = max(angleToSun - halfAngle, 0.0f);
        float3 glowTransmittance = min(sunTransmittance, float3(0.15f, 0.15f, 0.15f));
        float3 glowRadiance = glowTransmittance * gAtmosphere.sunDiskLuminanceScale * 0.02f;
        float glowFalloff = exp(-glowAngle / (halfAngle * 3.0f));

        luminance += disk * sunRadiance + glowFalloff * glowRadiance;
    }

    output.color.rgb = luminance * gAtmosphere.sunColor * gAtmosphere.sunIntensity;
    output.color.a = 1.0f;
    return output;
}

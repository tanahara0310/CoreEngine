/// @file CloudShadowMap.CS.hlsl
/// @brief 雲シャドウマップの生成（ゴッドレイ用）
/// @details 各テクセル = 雲底面（shadowAnchorWorldY）上のワールド XZ から太陽方向へ
///          雲層を貫いたときの透過率。カメラ XZ 追従の正方形領域（テクセルスナップ済み）を
///          上面図としてカバーする。GodRayMarch.CS がマーチ中の各点の太陽遮蔽として引く。
///          雲がアクティブなフレームは毎回再生成される（風・太陽・カメラで常に変わるため）。

#include "Common/CloudCommon.hlsli"
#include "Common/GodRayCommon.hlsli"

ConstantBuffer<CloudConstants> gCloud : register(b0);
ConstantBuffer<GodRayConstants> gGodRay : register(b1);
Texture3D<float4> gBaseShapeNoise : register(t0);
Texture2D<float4> gWeatherMap : register(t1);
SamplerState gSamplerLinearWrap : register(s0);
RWTexture2D<float> gCloudShadowMap : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= CLOUD_SHADOW_MAP_SIZE || dtid.y >= CLOUD_SHADOW_MAP_SIZE)
    {
        return;
    }

    float2 uv = (float2(dtid.xy) + 0.5f) / CLOUD_SHADOW_MAP_SIZE;
    float2 worldXZ = float2(gGodRay.shadowRegionCenterX, gGodRay.shadowRegionCenterZ)
                   + (uv - 0.5f) * gGodRay.shadowRegionSizeM;

    float3 toSun = -gCloud.sunDirection;

    // 太陽が地平線下: 雲影は定義できないが、光自体が PlanetSunVisibility（アースシャドウ）で
    // 消えるため「遮蔽なし」を書いておけば ΔL = 0 になり無害。
    if (toSun.y <= 0.0f)
    {
        gCloudShadowMap[dtid.xy] = 1.0f;
        return;
    }

    // 雲底面上の点から太陽方向へ、雲層外殻を抜けるまでマーチする。
    // 起点は内殻（雲底シェル）上にあるため RaySphere(rOut).y は必ず正。
    float3 pos = float3(worldXZ.x, gGodRay.shadowAnchorWorldY, worldXZ.y);
    float3 center = CloudPlanetCenter(gCloud);
    float rOut = gCloud.planetRadiusM + gCloud.layerBottomAltitudeM + gCloud.layerThicknessM;
    float tExit = RaySphere(pos, toSun, center, rOut).y;

    // 太陽が低いとき斜め経路が際限なく伸びるのをクランプ（層厚の 8 倍まで）
    tExit = clamp(tExit, 0.0f, gCloud.layerThicknessM * 8.0f);

    const int kStepCount = 24;
    float dt = tExit / kStepCount;

    // cheap 密度（ディテール侵食なし）で光学的深さを積む
    float tau = 0.0f;
    [loop] for (int i = 0; i < kStepCount; ++i)
    {
        float3 p = pos + toSun * ((i + 0.5f) * dt);
        float hf = CloudHeightFraction(p, gCloud);
        tau += SampleCloudDensityCheap(p, hf, gCloud,
            gBaseShapeNoise, gWeatherMap, gSamplerLinearWrap)
            * gCloud.densityScale * dt;
    }

    gCloudShadowMap[dtid.xy] = exp(-min(tau, 20.0f));
}

/// @file SkyEnvironmentCapture.CS.hlsl
/// @brief Sky-View LUT を小型キューブマップへ焼く（Phase 3b: スペキュラIBL / 空の映り込み）
/// @details UE の Sky Light (Real-Time Capture) のスペキュラ成分の入力に相当する。
///          各テクセルの方向で Sky-View LUT をサンプリングし、空の放射輝度を
///          キューブマップへ書き込む。輝度は SkyAtmosphere.PS と同じスケール
///          （sunColor × sunIntensity 焼き込み済み）。
///          太陽ディスクは含めない（UE と同じ方針。含めるとプリフィルタで
///          ファイアフライ状の白斑が出る。太陽ハイライトは直接光スペキュラが担う）。
///          α には 1.0（雲なし＝透過率1）を書き、後段の CloudCubemapCapture.CS が
///          雲の前乗算合成で上書きする。
///          Sky-View LUT 再生成時、および雲アニメーション中は毎フレーム実行される。

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gSkyViewLUT : register(t0);
SamplerState gLUTSampler : register(s0);
RWTexture2DArray<float4> gSkyCubemap : register(u0);

/// @brief キューブマップ面IDと2D座標から方向ベクトルを計算（DirectX標準座標系）
/// @details IBL/PrefilterEnvironment.CS.hlsl と同じマッピング。
float3 GetCubemapDirection(uint faceIndex, float2 uv)
{
    float2 texCoord = uv * 2.0f - 1.0f;
    float3 dir;
    switch (faceIndex)
    {
        case 0: dir = float3(1.0f, -texCoord.y, -texCoord.x); break;  // +X
        case 1: dir = float3(-1.0f, -texCoord.y, texCoord.x); break;  // -X
        case 2: dir = float3(texCoord.x, 1.0f, texCoord.y); break;    // +Y
        case 3: dir = float3(texCoord.x, -1.0f, -texCoord.y); break;  // -Y
        case 4: dir = float3(texCoord.x, -texCoord.y, 1.0f); break;   // +Z
        case 5: dir = float3(-texCoord.x, -texCoord.y, -1.0f); break; // -Z
        default: dir = float3(0.0f, 1.0f, 0.0f); break;
    }
    return normalize(dir);
}

/// @brief 指定方向の空の輝度を Sky-View LUT から取得する（SkyIrradianceSH.CS.hlsl と同じマッピング）
float3 SampleSkyLuminance(float3 dir)
{
    const float radiusKm = clamp(gAtmosphere.cameraRadiusKm,
        gAtmosphere.planetRadiusKm + 0.001f,
        gAtmosphere.atmosphereTopRadiusKm - 0.001f);

    const float3 toSun = -gAtmosphere.sunDirection;

    const float viewZenithCos = dir.y;

    // 太陽との相対方位角余弦（水平面内）
    float2 viewHorizontal = dir.xz;
    float2 sunHorizontal = toSun.xz;
    const float viewHorizontalLen = length(viewHorizontal);
    const float sunHorizontalLen = length(sunHorizontal);
    float lightViewCos = 1.0f;
    if (viewHorizontalLen > 1e-5f && sunHorizontalLen > 1e-5f)
    {
        lightViewCos = dot(viewHorizontal / viewHorizontalLen, sunHorizontal / sunHorizontalLen);
    }

    const float cosHorizon = -sqrt(max(0.0f,
        1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
    const bool intersectGround = (viewZenithCos < cosHorizon);

    const float2 uv = SkyViewParamsToUv(intersectGround, viewZenithCos, lightViewCos,
                                        radiusKm, gAtmosphere.planetRadiusKm);

    // 本番描画（SkyAtmosphere.PS）と同じ輝度スケールへ揃える
    return gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb
         * gAtmosphere.sunColor * gAtmosphere.sunIntensity;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width, height, faces;
    gSkyCubemap.GetDimensions(width, height, faces);
    if (dtid.x >= width || dtid.y >= height || dtid.z >= 6)
    {
        return;
    }

    const float2 uv = (float2(dtid.xy) + 0.5f) / float2(width, height);
    const float3 dir = GetCubemapDirection(dtid.z, uv);

    gSkyCubemap[dtid] = float4(SampleSkyLuminance(dir), 1.0f);
}

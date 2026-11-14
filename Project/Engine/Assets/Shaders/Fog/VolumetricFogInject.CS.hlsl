/// @file VolumetricFogInject.CS.hlsl
/// @brief 格子 1 つずつに霧の散乱係数と消散係数、発光を書く
/// @details 高さフォグは格子の中心を通る視線で、スライスの手前から奥までの光学的深さを積分し、
///          区間の長さで割った平均の消散係数にする。
///          局所の霧（箱・球）とノイズは、LightScattering と同じく格子の中で毎フレームずらした点で求める。
///          出力: gVBuffer の rgb = 散乱係数 σs、a = 消散係数 σt [1/m]。gEmissive の rgb = 1m あたりの発光。

#include "Fog.hlsli"
#include "VolumetricFogCommon.hlsli"
#include "DepthReconstruction.hlsli"

ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b0);
ConstantBuffer<VolumetricFogMedia> gVolumetricFogMedia : register(b1);
Texture3D<float4> gNoise : register(t0);
SamplerState gNoiseSampler : register(s0);
RWTexture3D<float4> gVBuffer : register(u0);
RWTexture3D<float3> gEmissive : register(u1);

// 雲のベース形状ノイズを 0.7R + 0.3G で混ぜた値の平均と標準偏差
static const float kNoiseMean = 0.666f;
static const float kNoiseDeviation = 0.066f;
// ノイズの強さ 1 のときの、濃さの倍率の標準偏差
static const float kNoiseContrast = 0.6f;

/// @brief 点のノイズの値（平均 0・標準偏差 1 にそろえた値）。格子の大きさに合うミップで引く
/// @param cellSize 格子の幅と厚さの小さいほう [m]
float SampleNoise(float3 worldPos, float cellSize)
{
    uint width;
    uint height;
    uint depth;
    uint levels;
    gNoise.GetDimensions(0, width, height, depth, levels);
    const float invScale = gVolumetricFogMedia.noiseInvScale;
    const float3 uvw = (worldPos + gVolumetricFogMedia.noiseOffset) * invScale;
    const float lod = clamp(log2(max(cellSize * invScale * float(width), 1.0f)), 0.0f, float(levels - 1u));
    const float4 noise = gNoise.SampleLevel(gNoiseSampler, uvw, lod);
    return (saturate(0.7f * noise.r + 0.3f * noise.g) - kNoiseMean) / kNoiseDeviation;
}

/// @brief ノイズで濃さへ掛ける倍率（平均でおよそ 1。薄い所は 0 まで下がる）
float NoiseFactor(float noise, float strength)
{
    return max(1.0f + kNoiseContrast * strength * noise, 0.0f);
}

/// @brief 局所の霧の中での濃さの割合（縁のぼかしの内側で 1・形の外で 0）
float VolumeWeight(VolumetricFogLocalVolume volume, float3 worldPos)
{
    const float3 offset = worldPos - volume.center;
    const float3 local = float3(dot(offset, volume.axisX), dot(offset, volume.axisY), dot(offset, volume.axisZ)) * 2.0f;
    const float fade = max(volume.edgeFade, 1.0e-4f);

    float weight;
    if (volume.shape == kVolumetricFogVolumeSphere)
    {
        const float inside = 1.0f - length(local);
        if (inside <= 0.0f)
        {
            return 0.0f;
        }
        weight = saturate(inside / fade);
    }
    else
    {
        const float3 inside = 1.0f - abs(local);
        if (any(inside <= 0.0f))
        {
            return 0.0f;
        }
        const float3 axes = saturate(inside / fade);
        weight = axes.x * axes.y * axes.z;
    }
    return weight * weight * (3.0f - 2.0f * weight);
}

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const VolumetricFogParameters p = gVolumetricFog;
    if (any(cell >= p.gridSize))
    {
        return;
    }

    const float2 uv = (float2(cell.xy) + 0.5f) * p.gridPixelSize;
    const float3 farPoint = ReconstructWorldPosition(ScreenUVToNDC(uv), 1.0f, p.invViewProj);
    const float3 viewDir = normalize(farPoint - p.cameraWorldPos);

    const float d0 = max(VolumetricFogSliceToDistance(p, float(cell.z)), p.startDistance);
    const float d1 = max(VolumetricFogSliceToDistance(p, float(cell.z) + 1.0f), p.startDistance);
    const float sliceLength = VolumetricFogSliceToDistance(p, float(cell.z) + 1.0f)
                            - VolumetricFogSliceToDistance(p, float(cell.z));

    FogParameters fog = (FogParameters)0;
    fog.density = p.density;
    fog.heightFalloff = p.heightFalloff;
    fog.heightRef = p.heightRef;
    const float opticalDepth = FogOpticalDepth(fog, p.cameraWorldPos, viewDir, d0, d1);

    float extinction = opticalDepth / max(sliceLength, 1.0e-4f);
    float3 scattering = extinction * p.albedo;
    float3 emission = float3(0.0f, 0.0f, 0.0f);

    if ((p.mediaFlags & kVolumetricFogMediaVarying) != 0)
    {
        const float2 jitteredUV = (float2(cell.xy) + 0.5f + p.frameJitter.xy) * p.gridPixelSize;
        const float3 jitteredFar = ReconstructWorldPosition(ScreenUVToNDC(jitteredUV), 1.0f, p.invViewProj);
        const float3 jitteredDir = normalize(jitteredFar - p.cameraWorldPos);
        const float3 worldPos = p.cameraWorldPos
            + jitteredDir * VolumetricFogSliceToDistance(p, float(cell.z) + p.frameJitter.z);

        const bool noiseEnabled = gVolumetricFogMedia.noiseEnabled != 0;
        float noise = 0.0f;
        if (noiseEnabled)
        {
            const float2 neighborUV = uv + float2(p.gridPixelSize.x, 0.0f);
            const float3 neighborFar = ReconstructWorldPosition(ScreenUVToNDC(neighborUV), 1.0f, p.invViewProj);
            const float3 neighborDir = normalize(neighborFar - p.cameraWorldPos);
            const float cellWidth = 0.5f * (d0 + d1) * length(neighborDir - viewDir);
            noise = SampleNoise(worldPos, min(cellWidth, sliceLength));
        }
        if (noiseEnabled)
        {
            const float factor = NoiseFactor(noise, gVolumetricFogMedia.noiseStrength);
            extinction *= factor;
            scattering *= factor;
        }

        [loop]
        for (uint i = 0; i < gVolumetricFogMedia.volumeCount; ++i)
        {
            const VolumetricFogLocalVolume volume = gVolumetricFogMedia.volumes[i];
            float weight = VolumeWeight(volume, worldPos);
            if (weight <= 0.0f)
            {
                continue;
            }
            if (noiseEnabled)
            {
                weight *= NoiseFactor(noise, volume.noiseStrength);
            }
            extinction += volume.extinction * weight;
            scattering += volume.albedo * (volume.extinction * weight);
            emission += volume.emissive * weight;
        }
    }

    gVBuffer[cell] = float4(scattering, extinction);
    if ((p.mediaFlags & kVolumetricFogMediaEmissive) != 0)
    {
        gEmissive[cell] = emission;
    }
}

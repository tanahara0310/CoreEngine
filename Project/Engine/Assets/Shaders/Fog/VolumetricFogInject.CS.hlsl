/// @file VolumetricFogInject.CS.hlsl
/// @brief 格子 1 つずつに霧の散乱係数と消散係数を書く
/// @details 格子の中心を通る視線で、スライスの手前から奥までの光学的深さを高さフォグの式で積分し、
///          区間の長さで割った平均の消散係数を書く。出力: rgb = 散乱係数 σs、a = 消散係数 σt [1/m]。

#include "Fog.hlsli"
#include "VolumetricFogCommon.hlsli"
#include "DepthReconstruction.hlsli"

ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b0);
RWTexture3D<float4> gVBuffer : register(u0);

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

    const float extinction = opticalDepth / max(sliceLength, 1.0e-4f);
    gVBuffer[cell] = float4(extinction * p.albedo, extinction);
}

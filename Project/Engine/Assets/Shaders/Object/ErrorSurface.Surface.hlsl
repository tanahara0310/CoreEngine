// 3D モデルのマテリアルを描けないとき（シェーダの誤り・.mat が読めない）の色
#include "ModelSurface.hlsli"

void Surface(SurfaceInput IN, inout SurfaceOutput OUT)
{
    OUT.baseColor = float3(1.0f, 0.0f, 1.0f);
    OUT.alpha = 1.0f;
    OUT.metallic = 0.0f;
    OUT.roughness = 1.0f;
    OUT.occlusion = 1.0f;
    OUT.emission = float3(1.0f, 0.0f, 1.0f);
}

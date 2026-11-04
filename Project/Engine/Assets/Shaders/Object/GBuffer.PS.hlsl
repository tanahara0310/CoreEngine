// 不透明モデル（BlendMode::None）の GBuffer 書き込み。材質は ModelSurface.hlsli の既定の値のまま
// ライティングは DeferredLighting.PS.hlsl で行う。

#define MODEL_SURFACE_CUSTOM_ENTRY_POINTS
#include "ModelSurface.hlsli"

// 1 = 動く物（スキンメッシュ）。GBufferSkinning.PS.hlsl が 1 にして include する
#ifndef GBUFFER_MOVING_OBJECT
#define GBUFFER_MOVING_OBJECT 0
#endif

void Surface(SurfaceInput IN, inout SurfaceOutput OUT)
{
}

GBufferOutput main(VertexShaderOutput input)
{
    return SurfaceGBuffer(input, (float)GBUFFER_MOVING_OBJECT);
}

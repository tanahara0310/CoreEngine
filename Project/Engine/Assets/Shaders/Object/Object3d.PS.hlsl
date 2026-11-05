// 半透明などの前方描画。材質は ModelSurface.hlsli の既定の値のまま

#define MODEL_SURFACE_CUSTOM_ENTRY_POINTS
#include "ModelSurface.hlsli"

void Surface(SurfaceInput IN, inout SurfaceOutput OUT)
{
}

PixelShaderOutput main(VertexShaderOutput input)
{
    return SurfaceForward(input);
}

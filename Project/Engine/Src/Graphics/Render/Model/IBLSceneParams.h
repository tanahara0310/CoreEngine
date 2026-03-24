#pragma once
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief gIBLParams 定数バッファのレイアウト（HLSL IBLSceneParams 構造体と一致）
    struct IBLSceneParamsCPU {
        float rotationX;
        float rotationY;
        float rotationZ;
        float padding;
    };
    static_assert(sizeof(IBLSceneParamsCPU) == 16, "IBLSceneParamsCPU size mismatch");
}

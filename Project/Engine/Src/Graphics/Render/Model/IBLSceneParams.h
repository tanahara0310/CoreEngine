#pragma once
#include <d3d12.h>
#include <cstdint>

namespace CoreEngine
{
    /// @brief gIBLParams 定数バッファのレイアウト（HLSL IBLSceneParams 構造体と一致）
    struct IBLSceneParamsCPU {
        float rotationX;
        float rotationY;
        float rotationZ;
        float environmentIntensity; ///< 環境輝度スケール（SkyBox intensity と連動）
        uint32_t sceneIBLEnabled;   ///< シーンに IBL マップ（Irradiance/Prefiltered/BRDF LUT）が揃っているか
        float padding[3];
    };
    static_assert(sizeof(IBLSceneParamsCPU) == 32, "IBLSceneParamsCPU size mismatch");
}

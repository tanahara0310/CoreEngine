#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "WaterRenderResources.h"

#include <d3d12.h>

namespace CoreEngine
{
    class CustomShaderPipeline;

/// @brief 水面シェーダ向けの GPU リソースバインド補助
/// @details WaterPlaneObject から SRV / CBV バインドの詳細を分離するためのクラス。
class WaterShaderResourceBinder {
public:
    /// @brief 水面描画に必要な定数バッファと SRV をコマンドリストへバインドする
    static void Bind(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline* pipeline,
        D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress,
        const WaterRenderResources& renderResources);
};
}

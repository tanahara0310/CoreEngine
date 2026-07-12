#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "WaterRenderResources.h"

#include <d3d12.h>

namespace CoreEngine {
    class CustomShaderPipeline;
}

/// @brief 水面シェーダ向けの GPU リソースバインド補助
/// @details WaterPlaneObject から SRV / CBV バインドの詳細を分離するためのクラス。
class WaterShaderResourceBinder {
public:
    /// @brief 水面描画に必要な定数バッファと SRV をコマンドリストへバインドする
    /// @param cmdList 描画コマンドリスト
    /// @param pipeline カスタムシェーダーパイプライン
    /// @param waterCBGpuAddress WaterConstants 用 CBV アドレス
    /// @param frameCBGpuAddress WaterFrameConstants 用 CBV アドレス
    /// @param frameConstants 現在のフレーム定数
    /// @param renderResources 水面描画で参照するテクスチャ群
    static void Bind(
        ID3D12GraphicsCommandList* cmdList,
        const CoreEngine::CustomShaderPipeline* pipeline,
        D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress,
        const WaterRenderResources& renderResources);
};

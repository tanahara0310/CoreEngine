#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    class AtmosphereManager;
    class CloudPipelines;
    class CloudResources;
    class GpuResource;

    /// @brief 雲の各レンダラーが 1 回の記録で必要とする参照一式
    /// @details 実体の所有は VolumetricCloudManager。レンダラーは記録するだけで状態を持たない。
    struct CloudRenderContext {
        ID3D12GraphicsCommandList* cmdList = nullptr;
        CloudResources* resources = nullptr;
        const CloudPipelines* pipelines = nullptr;
        const AtmosphereManager* atmosphere = nullptr;

        /// 雲 CB（gCloud）の GPU 仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS cloudConstants = 0;
        /// ゴッドレイ CB（gGodRay）の GPU 仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS godRayConstants = 0;

        /// @brief 半解像度バッファの実サイズで 8x8 スレッドグループをディスパッチする
        void DispatchHalfRes() const;

        /// @brief SceneColor へ合成 CS を in-place で走らせる
        /// @details SceneColor を UAV 状態にしてディスパッチし、後続パスの想定状態へ戻す。
        ///          合成 CS は各スレッドが自分のテクセルだけを読み書きするので中間バッファは要らない。
        void DispatchCompositeInPlace(GpuResource& sceneColor) const;
    };
}

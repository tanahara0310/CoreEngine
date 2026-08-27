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

        /// @brief 合成中間テクスチャの実サイズで 8x8 スレッドグループをディスパッチする
        void DispatchComposite() const;

        /// @brief 合成中間テクスチャを SceneColor へコピーバックし、後続パスの想定状態へ戻す
        void CopyCompositeToSceneColor(GpuResource& sceneColor) const;
    };
}

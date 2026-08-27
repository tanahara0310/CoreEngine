#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    class GpuResource;
    struct CloudRenderContext;

    /// @brief メインビューの雲（半解像度レイマーチ → SceneColor へ合成）
    class CloudRenderer {
    public:
        /// @brief 雲をレイマーチして SceneColor へ合成する
        /// @param sceneColor SceneColor（実体＋現在ステート）
        void Render(const CloudRenderContext& ctx,
                    GpuResource& sceneColor,
                    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
                    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle);
    };
}

#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    class GpuResource;
    struct CloudRenderContext;

    /// @brief ゴッドレイ（雲の隙間の光芒）
    /// @details 雲シャドウマップ生成 → 遮蔽差分のビューレイマーチ → SceneColor 合成。
    ///          合成モデルは差分法（遮蔽あり − 遮蔽なし ≤ 0 を加算）。
    ///          既存の Sky-View / Aerial Perspective が加算済みの「遮蔽なし内散乱」との
    ///          二重加算を避けつつ、雲影の空気柱を暗くして光芒の明暗対比を作る。
    class GodRayRenderer {
    public:
        /// @brief 雲シャドウマップ生成 → ゴッドレイマーチ → SceneColor 合成
        void Render(const CloudRenderContext& ctx,
                    GpuResource& sceneColor,
                    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUavHandle,
                    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle);
    };
}

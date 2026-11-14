#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    /// @brief 前方描画が引くボリュメトリックの格子の SRV（格子を使わないビュー・フレームは両方 0）
    /// @details FogPass が FogManager から受け取り、半透明・水面・パーティクルのレンダラーへ配る。
    ///          シェーダー側は FogForward.hlsli の gVolumetricFogIntegrated・gVolumetricFogFarLight（space6）。
    struct FogVolumetricResources {
        D3D12_GPU_DESCRIPTOR_HANDLE integratedSrv{}; ///< 積み上げた格子
        D3D12_GPU_DESCRIPTOR_HANDLE farLightSrv{};   ///< 格子の奥の光の画像

        /// @brief 差してよいか
        bool IsValid() const { return integratedSrv.ptr != 0 && farLightSrv.ptr != 0; }
    };
}

#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    /// @brief 前方描画が引くボリュメトリックの格子の SRV と格子より奥の霧の定数（格子を使わないビュー・フレームは全部 0）
    /// @details FogPass が FogManager から受け取り、半透明・水面・パーティクルのレンダラーへ配る。
    ///          シェーダー側は FogForward.hlsli の gVolumetricFogIntegrated・gVolumetricFogFarLight・
    ///          gVolumetricFogFarMedia（space6）。
    struct FogVolumetricResources {
        D3D12_GPU_DESCRIPTOR_HANDLE integratedSrv{}; ///< 積み上げた格子
        D3D12_GPU_DESCRIPTOR_HANDLE farLightSrv{};   ///< 格子の奥の光の画像
        D3D12_GPU_VIRTUAL_ADDRESS farMediaCbv = 0;   ///< 格子より奥の高さフォグのアルベドと局所の霧

        /// @brief 差してよいか
        bool IsValid() const { return integratedSrv.ptr != 0 && farLightSrv.ptr != 0 && farMediaCbv != 0; }
    };
}

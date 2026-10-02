#pragma once

#include "Graphics/RHI/Resource/PerFrameConstants.h"
#include "Graphics/Water/Surface/WaterSurfaceTypes.h"

#include <d3d12.h>

namespace CoreEngine
{
/// @brief Water 描画用の定数（WaterConstants / WaterFrameConstants）をまとめて持つ helper
/// @details 値は CPU 側に持ち、GPU へは記録中のフレームの UploadRing から渡す。
class WaterConstantBufferSet {
public:
    /// @brief 置き場所の UploadRing をつなぐ
    /// @param uploadRing 毎フレームの定数の置き場所
    void Initialize(UploadRing& uploadRing);

    /// @brief WaterConstants を記録中のフレームの UploadRing に置き、GPU 仮想アドレスを返す
    /// @note 返したアドレスはそのフレームの記録中だけ有効
    D3D12_GPU_VIRTUAL_ADDRESS GetWaterCBGpuAddress() const;

    /// @brief WaterFrameConstants を記録中のフレームの UploadRing に置き、GPU 仮想アドレスを返す
    /// @note 返したアドレスはそのフレームの記録中だけ有効
    D3D12_GPU_VIRTUAL_ADDRESS GetFrameCBGpuAddress() const;

    /// @brief WaterConstants を差し替える
    /// @param waterConstants CPU 側定数
    void UpdateWaterConstants(const WaterConstants& waterConstants);

    /// @brief WaterFrameConstants を差し替える
    /// @param frameConstants CPU 側定数
    void UpdateFrameConstants(const WaterFrameConstants& frameConstants);

private:
    PerFrameConstants<WaterConstants> waterConstants_;
    PerFrameConstants<WaterFrameConstants> frameConstants_;
};
}

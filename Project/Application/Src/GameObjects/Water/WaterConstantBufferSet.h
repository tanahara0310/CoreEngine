#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"

#include <d3d12.h>
#include <wrl.h>

/// @brief Water 描画用の定数バッファ群をまとめて管理する helper
/// @details WaterPlaneObject から GPU バッファ生成・マップ・更新責務を切り離す。
class WaterConstantBufferSet {
public:
    /// @brief Water 用定数バッファ群を作成する
    /// @param device D3D12 デバイス
    void Initialize(ID3D12Device* device);

    /// @brief WaterConstants の GPU 仮想アドレスを返す
    D3D12_GPU_VIRTUAL_ADDRESS GetWaterCBGpuAddress() const;

    /// @brief WaterFrameConstants の GPU 仮想アドレスを返す
    D3D12_GPU_VIRTUAL_ADDRESS GetFrameCBGpuAddress() const;

    /// @brief WaterConstants を GPU へ転送する
    /// @param waterConstants 転送元の CPU 側定数
    void UpdateWaterConstants(const WaterConstants& waterConstants);

    /// @brief WaterFrameConstants を GPU へ転送する
    /// @param frameConstants 転送元の CPU 側定数
    void UpdateFrameConstants(const WaterFrameConstants& frameConstants);

private:
    static constexpr UINT kWaterCBSize = (sizeof(WaterConstants) + 255) & ~255;
    static constexpr UINT kFrameCBSize = (sizeof(WaterFrameConstants) + 255) & ~255;

    /// @brief Upload Heap 上のバッファを 1 本作成してマップする
    void CreateBuffer(
        ID3D12Device* device,
        UINT bufferSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress,
        uint8_t*& mappedData);

    Microsoft::WRL::ComPtr<ID3D12Resource> waterCBResource_;
    D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress_ = 0;
    uint8_t* waterCBMapped_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> frameCBResource_;
    D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress_ = 0;
    uint8_t* frameCBMapped_ = nullptr;
};

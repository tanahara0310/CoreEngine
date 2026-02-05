#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

/// @brief グリフ（文字）のデータ構造

namespace CoreEngine
{
struct Glyph {
    /// @brief グリフのテクスチャリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    
    /// @brief アップロードバッファ（コマンド実行まで保持）
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    
    /// @brief GPU側のディスクリプタハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    
    /// @brief CPU側のディスクリプタハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    
    /// @brief グリフの幅（ピクセル）
    uint32_t width = 0;
    
    /// @brief グリフの高さ（ピクセル）
    uint32_t height = 0;
    
    /// @brief ベースラインからの水平オフセット（ピクセル）
    int32_t bearingX = 0;
    
    /// @brief ベースラインからの垂直オフセット（ピクセル）
    int32_t bearingY = 0;
    
    /// @brief 次の文字への移動量（1/64ピクセル単位）
    int32_t advance = 0;
};
}

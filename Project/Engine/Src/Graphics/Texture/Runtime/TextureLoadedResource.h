#pragma once

#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    /// @brief テクスチャロード済みGPUリソースを保持する共通データ構造
    struct TextureLoadedResource
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    };
}

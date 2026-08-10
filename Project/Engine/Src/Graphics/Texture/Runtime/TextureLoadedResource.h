#pragma once

#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    /// @brief テクスチャロード済みGPUリソースを保持する共通データ構造
    /// @note アップロード用の中間バッファはここでは持たない。
    ///       以前は「コピー完了が保証できないので永久に握る」しかなく、UPLOAD ヒープ
    ///       （システムメモリ）がテクスチャ枚数ぶん residents になっていた。
    ///       今は UploadContext がフェンスで完了を検知して解放する。
    struct TextureLoadedResource
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    };
}

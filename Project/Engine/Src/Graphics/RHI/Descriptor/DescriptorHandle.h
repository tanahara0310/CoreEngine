#pragma once

#include <d3d12.h>
#include <cstdint>

namespace CoreEngine
{

    /// @brief ディスクリプタヒープの種別
    enum class DescriptorHeapType : uint8_t
    {
        SRV_CBV_UAV, ///< CBV/SRV/UAV（シェーダー可視）
        RTV,         ///< レンダーターゲット
        DSV,         ///< 深度ステンシル
        Invalid,
    };

    /// @brief DescriptorAllocator が確保したスロットを表す値型
    ///
    /// - インデックス・ヒープ種別・CPU/GPU ハンドルを一体化する
    /// - `Free()` に渡した後は `IsValid()` が false になる
    /// - RTV/DSV の `gpuHandle.ptr` は 0（シェーダー不可視のため無効）
    struct DescriptorHandle
    {
        UINT index = UINT_MAX;          ///< ヒープ内スロットインデックス
        DescriptorHeapType heapType = DescriptorHeapType::Invalid;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { 0 };             ///< SRV/CBV/UAV のみ有効

        /// @brief 有効なスロットを指しているか
        bool IsValid() const { return heapType != DescriptorHeapType::Invalid && index != UINT_MAX; }

        /// @brief 無効化（Free 後に呼ばれる）
        void Invalidate()
        {
            index = UINT_MAX;
            heapType = DescriptorHeapType::Invalid;
            cpuHandle = { 0 };
            gpuHandle = { 0 };
        }
    };

} // namespace CoreEngine

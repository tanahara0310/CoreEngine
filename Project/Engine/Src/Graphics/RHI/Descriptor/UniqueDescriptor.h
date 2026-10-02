#pragma once

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"

namespace CoreEngine
{
    class DescriptorAllocator;

    /// @brief ディスクリプタのスロットを 1 つ持つハンドル。手放すと GPU が使い終わってから確保器へ返る
    /// @details ムーブだけできる。返すのは記録中のフレームの GPU 作業が終わった後。
    class UniqueDescriptor
    {
    public:
        UniqueDescriptor() = default;

        /// @param allocator スロットを確保した確保器（返す先）
        /// @param handle 確保したスロット
        UniqueDescriptor(DescriptorAllocator& allocator, const DescriptorHandle& handle) noexcept
            : allocator_(&allocator), handle_(handle) {}

        ~UniqueDescriptor() { Reset(); }

        UniqueDescriptor(const UniqueDescriptor&) = delete;
        UniqueDescriptor& operator=(const UniqueDescriptor&) = delete;

        UniqueDescriptor(UniqueDescriptor&& other) noexcept;
        UniqueDescriptor& operator=(UniqueDescriptor&& other) noexcept;

        /// @brief スロットを手放す（GPU が使い終わってから確保器へ返る）
        void Reset();

        /// @brief スロットを持っているか
        bool IsValid() const noexcept { return handle_.IsValid(); }

        /// @brief 持っているスロット
        const DescriptorHandle& Get() const noexcept { return handle_; }

        /// @brief CPU ハンドル（ビューの書き込みと RTV / DSV の設定に使う）
        const D3D12_CPU_DESCRIPTOR_HANDLE& Cpu() const noexcept { return handle_.cpuHandle; }

        /// @brief GPU ハンドル（SRV / CBV / UAV のみ有効）
        D3D12_GPU_DESCRIPTOR_HANDLE Gpu() const noexcept { return handle_.gpuHandle; }

        /// @brief ヒープ内のスロット番号
        UINT Index() const noexcept { return handle_.index; }

    private:
        DescriptorAllocator* allocator_ = nullptr;
        DescriptorHandle handle_{};
    };
}

#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"

namespace CoreEngine
{
    /// @brief ディスクリプタヒープ 1 本ぶんのスロット確保器（ヒープ種別ごとに 1 インスタンス）
    /// @note 確保・解放はスレッドセーフ
    class DescriptorHeapAllocator
    {
    public:
        /// @brief 初期化
        /// @param type      ヒープ種別（シェーダ可視かどうかはここから決まる）
        /// @param capacity  スロット数
        /// @param debugName ログに出す名前
        void Initialize(ID3D12Device* device, DescriptorHeapType type, uint32_t capacity,
                        std::string_view debugName);

        void Shutdown();

        /// @brief スロットを 1 つ確保する（フリーリスト優先）
        /// @param debugName 用途名（デバッグログ用）
        /// @throws std::runtime_error ヒープが満杯のとき
        DescriptorHandle Allocate(std::string_view debugName);

        /// @brief スロットを解放してフリーリストへ返す
        /// @note GPU がそのスロットを参照し終えた後（フェンス完了後）に呼ぶこと
        /// @param handle 解放するハンドル（呼び出し後は Invalidate される）
        void Free(DescriptorHandle& handle);

        /// @brief 確保済みインデックスからハンドルを再構成する
        DescriptorHandle HandleAt(uint32_t index) const;

        // ── 参照 ────────────────────────────────────────────────
        ID3D12DescriptorHeap* Heap() const { return heap_.Get(); }
        DescriptorHeapType Type() const noexcept { return type_; }
        uint32_t Capacity() const noexcept { return capacity_; }

        /// @brief 現在確保中のスロット数
        uint32_t LiveCount() const;

        /// @brief これまでに到達した最大確保数（フリーリスト再利用を考慮しない上限側の指標）
        uint32_t HighWaterMark() const;

        /// @brief 使用率 0..1（LiveCount / Capacity）
        float UsageRate() const;

    private:
        DescriptorHandle MakeHandle(uint32_t index) const;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
        ID3D12Device* device_ = nullptr;

        DescriptorHeapType type_ = DescriptorHeapType::Invalid;
        bool shaderVisible_ = false;
        uint32_t capacity_ = 0;
        uint32_t descriptorSize_ = 0;
        std::string debugName_;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart_{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart_{};

        mutable std::mutex mutex_;
        uint32_t nextIndex_ = 0;            ///< 未使用領域の先頭
        std::vector<uint32_t> freeIndices_; ///< 解放済みスロット（再利用する）

#ifdef _DEBUG
        std::vector<bool> allocated_;       ///< 二重解放検出
#endif
        bool warnedHighUsage_ = false;      ///< 使用率警告は 1 回だけ出す
    };
}

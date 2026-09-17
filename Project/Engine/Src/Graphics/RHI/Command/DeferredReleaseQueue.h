#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"

namespace CoreEngine
{
    class DescriptorAllocator;

    /// @brief GPU がまだ参照しているリソースとディスクリプタのスロットを、フェンス通過後に解放するキュー
    /// @note Push は呼び出し側が渡したフェンス値で寿命を判定する。
    ///       PushForCurrentFrame は記録中のフレームに預け、そのフレームの投入時に SealFrame が付けるフェンス値で判定する
    class DeferredReleaseQueue
    {
    public:
        /// @brief リソースを解放予約する
        /// @param resource 手放すリソース（ムーブされる）
        /// @param fenceValue この値まで GPU が進んだら解放してよい
        void Push(Microsoft::WRL::ComPtr<ID3D12Resource> resource, std::uint64_t fenceValue);

        /// @brief 記録中のフレームの GPU 作業が終わってからリソースを解放する予約をする
        /// @param resource 手放すリソース（ムーブされる）
        void PushForCurrentFrame(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

        /// @brief 記録中のフレームの GPU 作業が終わってからディスクリプタのスロットを返す予約をする
        /// @param allocator スロットを返す先
        /// @param handle 返すスロット（無効なら何もしない。預けた後は無効になる）
        void PushForCurrentFrame(DescriptorAllocator& allocator, DescriptorHandle& handle);

        /// @brief 記録中のフレームへ預けた予約に、そのフレームのフェンス値を付ける
        /// @param fenceValue 投入したフレームに発行したフェンス値（フレームを投入した直後に呼ぶ）
        void SealFrame(std::uint64_t fenceValue);

        /// @brief フェンス通過済みの予約を解放する（フレーム先頭で毎フレーム呼ぶ）
        /// @param completedFenceValue GPU が到達済みのフェンス値
        /// @return 解放した件数
        size_t Collect(std::uint64_t completedFenceValue);

        /// @brief 残っている予約をすべて解放する
        /// @warning GPU 完了を待った後にだけ呼ぶこと（シャットダウン専用）
        void ReleaseAll();

        /// @brief 未解放の予約件数（デバッグ表示用）
        size_t PendingCount() const;

    private:
        /// @brief まだフェンス値が付いていない予約の印
        static constexpr std::uint64_t kUnsealed = (std::numeric_limits<std::uint64_t>::max)();

        /// @brief 予約 1 件（リソースかディスクリプタのスロットのどちらか）
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            DescriptorAllocator* allocator = nullptr;
            DescriptorHandle descriptor{};
            std::uint64_t fenceValue = kUnsealed;
        };

        /// @brief 予約 1 件を解放する
        static void Release(Entry& entry);

        mutable std::mutex mutex_;
        std::vector<Entry> entries_;
    };
}

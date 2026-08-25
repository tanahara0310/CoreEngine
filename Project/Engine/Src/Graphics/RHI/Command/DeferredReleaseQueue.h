#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace CoreEngine
{
    /// @brief GPU がまだ参照しているリソースを、フェンス通過後に解放するキュー
    /// @note Push 時点の「最後に発行したフェンス値」で寿命を判定する
    ///       （＝この呼び出しより前に submit された作業が終われば解放してよい、の意味）
    class DeferredReleaseQueue
    {
    public:
        /// @brief リソースを解放予約する
        /// @param resource 手放すリソース（ムーブされる）
        /// @param fenceValue この値まで GPU が進んだら解放してよい
        void Push(Microsoft::WRL::ComPtr<ID3D12Resource> resource, std::uint64_t fenceValue);

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
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            std::uint64_t fenceValue = 0;
        };

        mutable std::mutex mutex_;
        std::vector<Entry> entries_;
    };
}

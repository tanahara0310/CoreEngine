#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace CoreEngine
{
    /// @brief GPU がまだ参照しているリソースを、フェンス通過後に解放するキュー
    ///
    /// @details
    /// 「GPU が読み終わってから解放する」を守る方法は 2 つある。
    ///   1. その場で GPU 完了を待つ（＝パイプラインが空になる。ストール）
    ///   2. 解放を予約し、フェンスが通ったフレームでまとめて捨てる（こちら）
    ///
    /// 従来は 1 をあちこちで手書きしていた（AccelerationStructureManager の
    /// FlushRetiredResources は、加速構造を作り直したフレームで毎回 GPU を待っていた）。
    /// このキューに預ければストールしない。
    ///
    /// @note Push した時点の「最後に発行したフェンス値」で寿命を判定する。
    ///       つまり「この呼び出しより前に submit された作業が終われば解放してよい」という意味になる。
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

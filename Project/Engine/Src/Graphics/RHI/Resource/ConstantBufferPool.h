#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

/// @file
/// @brief 長く使う小さな定数バッファを、大きなページから切り出して配るプール

namespace CoreEngine
{
    class DeferredReleaseQueue;

    /// @brief プールから借りた定数バッファの領域
    struct ConstantBufferBlock {
        void* cpu = nullptr;                        ///< 書き込み先（ページはずっとマップしたまま）
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;   ///< ルート CBV に渡すアドレス
        uint32_t page = 0;                          ///< 切り出したページの番号
        uint32_t slot = 0;                          ///< ページの中の番号
        uint32_t sizeClass = 0;                     ///< 大きさの段（256B × 2^sizeClass）

        bool IsValid() const noexcept { return cpu != nullptr; }
        void Invalidate() noexcept { *this = ConstantBufferBlock{}; }
    };

    /// @brief オブジェクトごとに持ち、ときどき書き換える小さな定数（マテリアルなど）を置くプール
    /// @details 64KB の UPLOAD ページを大きさの段ごとに作り、256B 単位で切り出す。
    ///          返した領域は、記録中のフレームの GPU 作業が終わってから使い回す。
    ///          どのスレッドから借りても返してもよい。
    class ConstantBufferPool {
    public:
        /// @brief 1 ページの大きさ
        static constexpr uint32_t kPageBytes = 64u * 1024u;
        /// @brief いちばん小さい段の大きさ（定数バッファの配置の単位）
        static constexpr uint32_t kMinBlockBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        /// @brief 段の数（256B〜64KB）
        static constexpr uint32_t kSizeClassCount = 9;

        ConstantBufferPool() = default;
        ~ConstantBufferPool();

        ConstantBufferPool(const ConstantBufferPool&) = delete;
        ConstantBufferPool& operator=(const ConstantBufferPool&) = delete;

        /// @brief 初期化
        void Initialize(ID3D12Device* device);

        /// @brief 返した領域を預ける先をつなぐ（無ければすぐ使い回す）
        void SetDeferredReleaseQueue(DeferredReleaseQueue* queue);

        /// @brief 全ページを手放す（GPU の完了を待った後に呼ぶ）
        /// @details 以後の Retire は何もしない
        void Shutdown();

        /// @brief 領域を 1 つ借りる（中身は 0 で埋める）
        /// @param size 必要なバイト数（64KB まで）
        /// @return 借りた領域。大きすぎる・初期化前のときは IsValid() が false
        ConstantBufferBlock Allocate(uint32_t size);

        /// @brief 領域を返す（記録中のフレームの GPU 作業が終わってから使い回す）
        /// @param block 返す領域（返した後は無効になる）
        void Retire(ConstantBufferBlock& block);

        // ── 統計（デバッグ表示用） ──────────────────────────────
        /// @brief 作ったページの数
        size_t PageCount() const;
        /// @brief 貸し出し中の領域の数
        size_t LiveBlockCount() const;

    private:
        friend class DeferredReleaseQueue;

        /// @brief 領域をすぐ使い回せる状態へ戻す（GPU が読み終えた後に、遅延解放キューから呼ばれる）
        void Reclaim(const ConstantBufferBlock& block);

        /// @brief 1 ページ（1 つの段の領域だけを切り出す）
        struct Page {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            uint8_t* cpu = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
            uint32_t sizeClass = 0;
        };

        /// @brief 空いている領域の場所
        struct FreeSlot {
            uint32_t page = 0;
            uint32_t slot = 0;
        };

        /// @brief 段 sizeClass のページを 1 枚足し、空きへ並べる（mutex_ を持った状態で呼ぶ）
        bool AppendPage(uint32_t sizeClass);

        ID3D12Device* device_ = nullptr;
        DeferredReleaseQueue* deferredRelease_ = nullptr;
        bool shutdown_ = false;

        mutable std::mutex mutex_;
        std::vector<Page> pages_;
        std::array<std::vector<FreeSlot>, kSizeClassCount> freeSlots_;
        size_t liveBlocks_ = 0;
    };
}

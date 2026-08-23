#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

/// @file
/// @brief フレームごとに使い捨てる CPU→GPU 転送メモリ（リニアアロケータ）

namespace CoreEngine
{
    /// @brief 1 回の確保結果
    struct UploadAllocation {
        void* cpu = nullptr;                        ///< 書き込み先（CPU から見えるアドレス）
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;   ///< ルート CBV / VBV / IBV に渡すアドレス
        uint32_t size = 0;                          ///< 確保できた大きさ

        bool IsValid() const noexcept { return cpu != nullptr; }
    };

    /// @brief フレーム単位で巻き戻る UPLOAD ヒープのリニアアロケータ
    ///
    /// @details
    /// **解決する問題**: 定数バッファを「クラスごとに 1 本作って常時 Map し、毎フレーム上書き」
    /// すると、CPU は GPU より 1 フレーム先行しているので **GPU が読んでいる最中の値を
    /// CPU が書き潰す**（フレーム N のパスがフレーム N+1 の行列を読む）。
    /// 実際に SSAO でこれを踏み、その場しのぎに `FrameRingConstantBuffer` という
    /// 同じ仕組みがレンダリング技術のヘッダの中に作られていた。ここはその一般化であり、
    /// 「per-frame 二重化」を各クラスが自前で意識しなくてよくするのが目的。
    ///
    /// **使い方**:
    /// @code
    /// MyConstants c{ ... };
    /// const auto cbAddress = graphicsCore_->GetUploadRing().AllocateConstants(c);
    /// cmdList->SetGraphicsRootConstantBufferView(index, cbAddress);
    /// @endcode
    /// リソースの生成・Map・メンバ保持・解放はどれも要らない。
    ///
    /// **寿命**: 確保したメモリが有効なのは **そのフレームの記録中だけ**。
    /// フレームスロットは framesInFlight 本あり、`Reset()` で巻き戻る。
    /// スロットの再利用は `FrameSync::AdvanceToNextFrame()` が該当スロットの
    /// フェンス完了を待った後なので、GPU がまだ読んでいる領域を踏むことはない。
    ///
    /// @warning 確保した領域を次のフレームへ持ち越さないこと（内容は上書きされる）。
    ///          持ち越したいものは通常どおり自前のバッファを持つこと。
    /// @warning 確保はコマンド記録スレッド（メインスレッド）からのみ。
    ///          ワーカースレッドからのアップロードは UploadContext を使うこと。
    ///
    /// @note UPLOAD ヒープ＝システムメモリ常駐なので、毎フレーム GPU が何度も読む
    ///       頂点／インデックスバッファをここへ置いてはならない（PCIe 帯域律速になる）。
    ///       用途は「毎フレーム内容が変わる小さな定数」。
    class UploadRing {
    public:
        /// @brief 1 フレームあたりの既定容量（足りなければ自動で増える）
        static constexpr uint32_t kDefaultBytesPerFrame = 1u * 1024u * 1024u;

        UploadRing() = default;
        ~UploadRing();

        UploadRing(const UploadRing&) = delete;
        UploadRing& operator=(const UploadRing&) = delete;

        /// @brief 初期化
        /// @param device D3D12 デバイス
        /// @param framesInFlight フレームスロット数（FrameSync::FramesInFlight() を渡す）
        /// @param bytesPerFrame 1 フレームあたりの初期容量
        void Initialize(ID3D12Device* device, uint32_t framesInFlight,
                        uint32_t bytesPerFrame = kDefaultBytesPerFrame);

        /// @brief 全ページを解放する
        void Shutdown();

        /// @brief 指定スロットの確保位置を先頭へ巻き戻す
        /// @param frameIndex これから記録するフレームのスロット番号
        /// @details 呼ぶのは **そのスロットの GPU 完了を待った後**。
        ///          GraphicsCore がコマンドアロケータの Reset と同じ場所で呼ぶ
        ///          （どちらも「このスロットはもう GPU が使っていない」が前提なので、
        ///           2 つの巻き戻しが別の場所にあると片方だけずれる）。
        void Reset(uint32_t frameIndex);

        /// @brief 生のバイト列を確保する
        /// @param size 必要なバイト数
        /// @param alignment 先頭アライメント（既定は定数バッファの 256B）
        /// @return 書き込み先と GPU アドレス。デバイス未初期化などで失敗すると IsValid() が false
        UploadAllocation Allocate(uint32_t size,
            uint32_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        /// @brief 定数バッファを 1 つ確保して内容をコピーする
        /// @return ルート CBV へ渡す GPU アドレス（失敗時は 0）
        D3D12_GPU_VIRTUAL_ADDRESS AllocateConstants(const void* src, uint32_t size);

        /// @brief 定数バッファを 1 つ確保して構造体をそのままコピーする
        /// @note HLSL 側のパッキングと一致していること（CB_VERIFY_LAYOUT で検証されている型を渡す）。
        ///       パッキングが一致しない型は Allocate() を使い Cb::Upload() で詰めること。
        template <class T>
        D3D12_GPU_VIRTUAL_ADDRESS AllocateConstants(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "UploadRing::AllocateConstants: 定数バッファ構造体は trivially copyable であること");
            return AllocateConstants(&value, static_cast<uint32_t>(sizeof(T)));
        }

        // ── 統計（デバッグ表示用） ──────────────────────────────
        /// @brief 今フレームで確保したバイト数
        uint32_t BytesUsedThisFrame() const noexcept;
        /// @brief 全フレームを通じた 1 フレームあたりの最大確保バイト数
        uint32_t PeakBytesPerFrame() const noexcept { return peakBytesPerFrame_; }
        /// @brief 1 フレームあたりの確保済み容量（自動拡張後の実容量）
        uint32_t CapacityPerFrame() const noexcept;
        /// @brief 容量不足でページを追加した回数（0 が理想）
        uint32_t GrowCount() const noexcept { return growCount_; }

    private:
        /// @brief 実体となる UPLOAD バッファ 1 枚（常時 Map）
        struct Page {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            uint8_t* cpuBase = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpuBase = 0;
            uint32_t capacity = 0;
        };

        /// @brief 1 フレームスロット分の状態
        struct FrameSlot {
            std::vector<Page> pages;   ///< 高水位まで増えたら以後は再利用される
            uint32_t pageIndex = 0;    ///< 今使っているページ
            uint32_t offset = 0;       ///< そのページ内の次の確保位置
            uint32_t bytesUsed = 0;    ///< 今フレームの確保量（統計）
        };

        /// @brief スロットへページを 1 枚足す
        bool AppendPage(FrameSlot& slot, uint32_t capacity);

        ID3D12Device* device_ = nullptr;
        std::vector<FrameSlot> slots_;
        uint32_t currentSlot_ = 0;
        uint32_t bytesPerPage_ = kDefaultBytesPerFrame;

        uint32_t peakBytesPerFrame_ = 0;
        uint32_t growCount_ = 0;
    };
}

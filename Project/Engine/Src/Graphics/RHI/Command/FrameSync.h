#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>

namespace CoreEngine
{
    /// @brief CPU が GPU に先行できるフレーム数の上限
    /// @details per-frame リソースは「配列は最大数・添字は FrameIndex()」で統一する
    inline constexpr uint32_t kMaxFramesInFlight = 3;

    /// @brief 1 フレーム分の同一性と記録先
    struct FrameContext
    {
        /// @brief per-frame リソースの添字（0 .. FramesInFlight()-1）
        /// @warning GetCurrentBackBufferIndex() で代用しないこと（ResizeBuffers で 0 に戻る）
        uint32_t frameIndex = 0;

        /// @brief 単調増加のフレーム番号（TAA のジッタ位相・履歴 ping-pong 等に使う）
        uint64_t frameNumber = 0;

        /// @brief 今フレームの記録先コマンドリスト
        ID3D12GraphicsCommandList* cmdList = nullptr;
    };

    /// @brief フレーム番号・フレーム数・per-frame フェンス値を持つ単一ソース
    class FrameSync
    {
    public:
        /// @brief 初期化
        /// @param queue Signal を投げるキュー
        /// @param framesInFlight CPU 先行フレーム数（[2, kMaxFramesInFlight] にクランプされる）
        void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t framesInFlight);

        /// @brief 終了処理（全 GPU 作業の完了を待ってからフェンスを解放する）
        void Shutdown();

        // ── フレームの同一性 ────────────────────────────────────
        uint32_t FramesInFlight() const noexcept { return framesInFlight_; }
        uint32_t FrameIndex()     const noexcept { return frameIndex_; }
        uint64_t FrameNumber()    const noexcept { return frameNumber_; }

        /// @brief フレーム開始（フレーム番号を進める）
        void BeginFrame() noexcept { ++frameNumber_; }

        // ── GPU 同期 ────────────────────────────────────────────

        /// @brief 現在のフレームスロットの完了をシグナルする（非ブロッキング）
        void SignalCurrentFrame();

        /// @brief 指定スロットの GPU 完了を待つ
        void WaitForFrame(uint32_t frameIndex);

        /// @brief 次のフレームスロットへ進む（そのスロットの完了を待ってから切り替える）
        void AdvanceToNextFrame();

        /// @brief 投入済みの全 GPU 作業の完了を待つ（完全同期）
        /// @details リソースの作り直し・破棄の直前にだけ使うこと
        void WaitForGpuIdle();

        // ── フェンス値（遅延解放が使う） ────────────────────────

        /// @brief 最後に発行したフェンス値
        uint64_t LastSignaledValue() const noexcept { return fenceValue_; }

        /// @brief GPU が到達済みのフェンス値
        uint64_t CompletedValue() const;

        /// @brief 指定スロットが最後に signal したフェンス値（0 = 未 submit）
        uint64_t FenceValueOf(uint32_t frameIndex) const;

    private:
        /// @brief フェンスイベントを待つ（GPU ハングを検出できる形で）
        /// @details 一定時間ごとに区切ってデバイスロストを確認し、失われていれば例外を投げる
        void WaitOnFenceEvent(std::uint64_t target);

        ID3D12Device* device_ = nullptr;
        ID3D12CommandQueue* queue_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        HANDLE fenceEvent_ = nullptr;
        std::uint64_t fenceValue_ = 0;

        std::array<std::uint64_t, kMaxFramesInFlight> frameFenceValues_{};

        uint32_t framesInFlight_ = 2;
        uint32_t frameIndex_ = 0;
        std::uint64_t frameNumber_ = 0;
    };
}

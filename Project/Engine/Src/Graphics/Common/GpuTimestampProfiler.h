#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <chrono>
#include <cstdint>

namespace CoreEngine
{
    /// @brief GPU タイムスタンプ計測スロット
    enum class GpuTimestampSlot : uint32_t
    {
        ShadowPass = 0,
        GBufferPass,
        RTShadow,
        SSAOPass,
        DeferredLighting,
        GeometryPass,
        PostEffect,
        BackBufferPass,
        ImGuiDraw,
        Total,
        Count
    };

    /// @brief 1スロット分の CPU / GPU 計測結果
    struct GpuTimingResult
    {
        const char* name = "";
        float cpuMs = 0.0f;
        float gpuMs = 0.0f;
    };

    /// @brief DirectX12 タイムスタンプクエリを用いた GPU プロファイラー
    ///
    /// 使い方:
    ///   1. Initialize(device)
    ///   2. 毎フレーム NewFrame(currentBackBufferIndex)
    ///   3. コマンドリスト記録中に BeginGpuTimestamp / EndGpuTimestamp
    ///   4. Close() 前に ResolveAll(cmdList, currentBackBufferIndex)
    ///   5. FinalizeFrame() 完了後に ReadResults(commandQueue, nextBackBufferIndex)
    class GpuTimestampProfiler
    {
    public:
        static constexpr uint32_t kSlotCount = static_cast<uint32_t>(GpuTimestampSlot::Count);
        static constexpr uint32_t kFrameCount = 2;
        static constexpr uint32_t kQueriesPerSlot = 2; // begin + end
        static constexpr uint32_t kQueriesPerFrame = kSlotCount * kQueriesPerSlot;

        /// @brief 初期化
        void Initialize(ID3D12Device* device);

        /// @brief 終了処理
        void Finalize();

        /// @brief フレーム開始時に呼ぶ (frameIndex を更新し CPU 計測をリセット)
        void NewFrame(uint32_t frameIndex);

        // ── GPU タイムスタンプ ──────────────────────────────────

        /// @brief スロット開始タイムスタンプをコマンドリストに積む
        void BeginGpuTimestamp(GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList);

        /// @brief スロット終了タイムスタンプをコマンドリストに積む
        void EndGpuTimestamp(GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList);

        /// @brief 全クエリを readback バッファへ解決 (cmdList->Close() 前に必須)
        void ResolveAll(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);

        /// @brief readback バッファから GPU 結果を読み出し lastResults_ に格納
        /// @note  WaitForFrame(readFrameIndex) 完了後に呼ぶこと
        void ReadResults(ID3D12CommandQueue* commandQueue, uint32_t readFrameIndex);

        // ── CPU タイムスタンプ ──────────────────────────────────

        /// @brief スロットの CPU 計測を開始
        void BeginCpuTimestamp(GpuTimestampSlot slot);

        /// @brief スロットの CPU 計測を終了
        void EndCpuTimestamp(GpuTimestampSlot slot);

        // ── 結果取得 ──────────────────────────────────────────

        /// @brief 直近フレームの計測結果を返す (1フレーム遅延あり)
        const std::array<GpuTimingResult, kSlotCount>& GetResults() const { return lastResults_; }

        bool IsInitialized() const { return initialized_; }

        /// @brief スロット番号に対応する表示名を返す
        static constexpr const char* GetSlotName(GpuTimestampSlot slot) noexcept
        {
            switch (slot)
            {
            case GpuTimestampSlot::ShadowPass:       return "Shadow Pass";
            case GpuTimestampSlot::GBufferPass:      return "GBuffer Pass";
            case GpuTimestampSlot::RTShadow:         return "RT Shadow";
            case GpuTimestampSlot::SSAOPass:         return "SSAO Pass";
            case GpuTimestampSlot::DeferredLighting: return "Deferred Lighting";
            case GpuTimestampSlot::GeometryPass:     return "Geometry Pass";
            case GpuTimestampSlot::PostEffect:       return "Post Effect";
            case GpuTimestampSlot::BackBufferPass:   return "BackBuffer Pass";
            case GpuTimestampSlot::ImGuiDraw:        return "ImGui Draw";
            case GpuTimestampSlot::Total:            return "Frame Total";
            default:                                 return "Unknown";
            }
        }

        /// @brief RAII スコープで CPU/GPU タイムスタンプを計測するガード
        class ProfileScope
        {
        public:
            ProfileScope(GpuTimestampProfiler& profiler, GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList)
                : profiler_(profiler), slot_(slot), cmdList_(cmdList)
            {
                profiler_.BeginCpuTimestamp(slot_);
                profiler_.BeginGpuTimestamp(slot_, cmdList_);
            }
            ~ProfileScope()
            {
                profiler_.EndGpuTimestamp(slot_, cmdList_);
                profiler_.EndCpuTimestamp(slot_);
            }
            ProfileScope(const ProfileScope&) = delete;
            ProfileScope& operator=(const ProfileScope&) = delete;
        private:
            GpuTimestampProfiler& profiler_;
            GpuTimestampSlot           slot_;
            ID3D12GraphicsCommandList* cmdList_;
        };

    private:
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
        Microsoft::WRL::ComPtr<ID3D12Resource>  readbackBuffers_[kFrameCount];

        uint32_t currentFrameIndex_ = 0;

        // フレーム内で実際に BeginGpuTimestamp されたスロットを追跡するビットマスク
        uint32_t activatedSlots_[kFrameCount] = {};

        // CPU タイム (ダブルバッファ: [frameIndex][slotIndex])
        std::chrono::high_resolution_clock::time_point cpuBegin_[kFrameCount][kSlotCount] = {};
        float cpuTimesMs_[kFrameCount][kSlotCount] = {};

        std::array<GpuTimingResult, kSlotCount> lastResults_ = {};
        bool initialized_ = false;
    };
}

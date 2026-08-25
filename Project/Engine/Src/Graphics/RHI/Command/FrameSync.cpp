#include "pch.h"
#include "Graphics/RHI/Command/FrameSync.h"

#include "Graphics/RHI/Device/DeviceRemovedHandler.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace CoreEngine
{
    namespace
    {
        /// @brief フェンス待ちを区切る間隔
        /// @details 「まだ描いている」のか「GPU が死んだ」のかは待っているだけでは区別できない。
        ///          この間隔で目を覚まし、デバイスが生きているかを確認する。
        ///          正常時は 1 フレーム（〜16ms）で signal されるので、この値に達すること自体が異常。
        constexpr DWORD kFenceWaitSliceMs = 5000;
    }

    void FrameSync::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t framesInFlight)
    {
        assert(device != nullptr && "Device must not be null");
        assert(queue != nullptr && "CommandQueue must not be null");

        device_ = device;
        queue_ = queue;
        framesInFlight_ = std::clamp(framesInFlight, 2u, kMaxFramesInFlight);

        HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        assert(SUCCEEDED(hr) && "FrameSync: failed to create fence");
        (void)hr;

        fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        assert(fenceEvent_ != nullptr && "FrameSync: failed to create fence event");

        fenceValue_ = 0;
        frameIndex_ = 0;
        frameNumber_ = 0;
        frameFenceValues_.fill(0);

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Command,
            "FrameSync初期化完了: framesInFlight={}（要求値={}）\n", framesInFlight_, framesInFlight);
    }

    void FrameSync::Shutdown()
    {
        if (queue_ && fence_) {
            // デバイスロストならここで例外が飛ぶ。原因は既にログへ出ているので、
            // 後片付けの途中で throw して terminate させない（デストラクタ経由で来るため）
            try {
                WaitForGpuIdle();
            } catch (const std::exception& e) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                    "FrameSync::Shutdown: GPU 待ちに失敗しましたが終了処理を続行します: {}", e.what());
            }
        }
        fence_.Reset();
        if (fenceEvent_) {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
        device_ = nullptr;
        queue_ = nullptr;
    }

    void FrameSync::SignalCurrentFrame()
    {
        if (!queue_ || !fence_) {
            return;
        }
        ++fenceValue_;
        frameFenceValues_[frameIndex_] = fenceValue_;
        queue_->Signal(fence_.Get(), fenceValue_);
    }

    void FrameSync::WaitForFrame(uint32_t frameIndex)
    {
        if (!fence_ || frameIndex >= framesInFlight_) {
            return;
        }
        const std::uint64_t target = frameFenceValues_[frameIndex];
        if (target == 0 || fence_->GetCompletedValue() >= target) {
            return; // 未 submit、または既に完了済み
        }
        const HRESULT hr = fence_->SetEventOnCompletion(target, fenceEvent_);
        assert(SUCCEEDED(hr));
        (void)hr;
        WaitOnFenceEvent(target);
    }

    void FrameSync::WaitOnFenceEvent(std::uint64_t target)
    {
        for (uint32_t round = 1; ; ++round) {
            if (WaitForSingleObject(fenceEvent_, kFenceWaitSliceMs) == WAIT_OBJECT_0) {
                return; // 通常はここ（1 フレーム分待つだけ）
            }

            // ここへ来たということは GPU が数秒応答していない。
            // デバイスロスト（TDR）なら永久に signal されないので、待ち続けてはいけない。
            if (ReportIfDeviceRemoved(device_, "FrameSync: GPU の完了待ち")) {
                throw std::runtime_error(
                    "GPU device removed while waiting for a fence (詳細は Graphics ログを参照)");
            }

            // デバイスは生きている＝単に重いだけ。無限に黙らないよう記録だけ残して待ち続ける
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Command,
                "FrameSync: GPU の完了待ちが {} 秒を超えました（target={} / completed={}）。"
                "デバイスは生きているので待機を続けます",
                (round * kFenceWaitSliceMs) / 1000, target, fence_->GetCompletedValue());
        }
    }

    void FrameSync::AdvanceToNextFrame()
    {
        const uint32_t next = (frameIndex_ + 1) % framesInFlight_;
        // これから記録するスロットを GPU がまだ読んでいる場合はここで待つ。
        WaitForFrame(next);
        frameIndex_ = next;
    }

    void FrameSync::WaitForGpuIdle()
    {
        if (!queue_ || !fence_) {
            return;
        }
        ++fenceValue_;
        queue_->Signal(fence_.Get(), fenceValue_);
        if (fence_->GetCompletedValue() < fenceValue_) {
            fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
            WaitOnFenceEvent(fenceValue_);
        }
    }

    uint64_t FrameSync::CompletedValue() const
    {
        return fence_ ? fence_->GetCompletedValue() : 0;
    }

    uint64_t FrameSync::FenceValueOf(uint32_t frameIndex) const
    {
        return (frameIndex < framesInFlight_) ? frameFenceValues_[frameIndex] : 0;
    }
}

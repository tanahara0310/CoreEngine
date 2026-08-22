#include "pch.h"
#include "Graphics/RHI/Command/FrameSync.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>

namespace CoreEngine
{
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
            WaitForGpuIdle();
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
        WaitForSingleObject(fenceEvent_, INFINITE);
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
            WaitForSingleObject(fenceEvent_, INFINITE);
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

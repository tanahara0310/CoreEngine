#include "pch.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"

#include <algorithm>

namespace CoreEngine
{
    void DeferredReleaseQueue::Push(Microsoft::WRL::ComPtr<ID3D12Resource> resource, std::uint64_t fenceValue)
    {
        if (!resource) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(Entry{ std::move(resource), fenceValue });
    }

    size_t DeferredReleaseQueue::Collect(std::uint64_t completedFenceValue)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.empty()) {
            return 0;
        }

        const size_t before = entries_.size();
        // 解放は ComPtr のデストラクタに任せる（erase で落ちる）
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [completedFenceValue](const Entry& e) { return e.fenceValue <= completedFenceValue; }),
            entries_.end());
        return before - entries_.size();
    }

    void DeferredReleaseQueue::ReleaseAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    size_t DeferredReleaseQueue::PendingCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }
}

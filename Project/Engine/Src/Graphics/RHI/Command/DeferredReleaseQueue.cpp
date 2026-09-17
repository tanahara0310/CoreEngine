#include "pch.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"

#include <utility>

namespace CoreEngine
{
    void DeferredReleaseQueue::Push(Microsoft::WRL::ComPtr<ID3D12Resource> resource, std::uint64_t fenceValue)
    {
        if (!resource) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Entry entry;
        entry.resource = std::move(resource);
        entry.fenceValue = fenceValue;
        entries_.push_back(std::move(entry));
    }

    void DeferredReleaseQueue::PushForCurrentFrame(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        if (!resource) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Entry entry;
        entry.resource = std::move(resource);
        entries_.push_back(std::move(entry));
    }

    void DeferredReleaseQueue::PushForCurrentFrame(DescriptorAllocator& allocator, DescriptorHandle& handle)
    {
        if (!handle.IsValid()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Entry entry;
        entry.allocator = &allocator;
        entry.descriptor = handle;
        entries_.push_back(std::move(entry));
        handle.Invalidate();
    }

    void DeferredReleaseQueue::SealFrame(std::uint64_t fenceValue)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Entry& entry : entries_) {
            if (entry.fenceValue == kUnsealed) {
                entry.fenceValue = fenceValue;
            }
        }
    }

    size_t DeferredReleaseQueue::Collect(std::uint64_t completedFenceValue)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.empty()) {
            return 0;
        }

        // フェンスを通過した予約を解放し、残りを前へ詰める（フェンス値の無い予約は残す）
        size_t released = 0;
        auto kept = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->fenceValue != kUnsealed && it->fenceValue <= completedFenceValue) {
                Release(*it);
                ++released;
                continue;
            }
            if (kept != it) {
                *kept = std::move(*it);
            }
            ++kept;
        }
        entries_.erase(kept, entries_.end());
        return released;
    }

    void DeferredReleaseQueue::ReleaseAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Entry& entry : entries_) {
            Release(entry);
        }
        entries_.clear();
    }

    size_t DeferredReleaseQueue::PendingCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    void DeferredReleaseQueue::Release(Entry& entry)
    {
        if (entry.allocator && entry.descriptor.IsValid()) {
            entry.allocator->Free(entry.descriptor);
        }
        entry.allocator = nullptr;
        entry.resource.Reset();
    }
}

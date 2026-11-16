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

    void DeferredReleaseQueue::PushForCurrentFrame(ConstantBufferPool& pool, ConstantBufferBlock& block)
    {
        if (!block.IsValid()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Entry entry;
        entry.pool = &pool;
        entry.block = block;
        entries_.push_back(std::move(entry));
        block.Invalidate();
    }

    void DeferredReleaseQueue::PushForCurrentFrame(std::shared_ptr<void> object)
    {
        if (!object) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Entry entry;
        entry.object = std::move(object);
        entries_.push_back(std::move(entry));
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
        // 預かっていた物は、鍵を外した後（この関数を出るとき）に壊す
        std::vector<std::shared_ptr<void>> objects;
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.empty()) {
            return 0;
        }

        // フェンスを通過した予約を解放し、残りを前へ詰める（フェンス値の無い予約は残す）
        size_t released = 0;
        auto kept = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->fenceValue != kUnsealed && it->fenceValue <= completedFenceValue) {
                Release(*it, objects);
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
        // 預かっていた物は、鍵を外した後（この関数を出るとき）に壊す
        std::vector<std::shared_ptr<void>> objects;
        std::lock_guard<std::mutex> lock(mutex_);
        for (Entry& entry : entries_) {
            Release(entry, objects);
        }
        entries_.clear();
    }

    size_t DeferredReleaseQueue::PendingCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    void DeferredReleaseQueue::Release(Entry& entry, std::vector<std::shared_ptr<void>>& objects)
    {
        if (entry.object) {
            objects.push_back(std::move(entry.object));
        }
        if (entry.allocator && entry.descriptor.IsValid()) {
            entry.allocator->Free(entry.descriptor);
        }
        entry.allocator = nullptr;
        if (entry.pool && entry.block.IsValid()) {
            entry.pool->Reclaim(entry.block);
        }
        entry.pool = nullptr;
        entry.block.Invalidate();
        entry.resource.Reset();
    }
}

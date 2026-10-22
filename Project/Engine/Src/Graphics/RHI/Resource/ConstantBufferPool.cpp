#include "pch.h"
#include "Graphics/RHI/Resource/ConstantBufferPool.h"

#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace CoreEngine
{
    namespace
    {
        /// @brief 段 sizeClass の 1 領域の大きさ
        constexpr uint32_t BlockBytes(uint32_t sizeClass)
        {
            return ConstantBufferPool::kMinBlockBytes << sizeClass;
        }

        /// @brief size バイトが入るいちばん小さい段（入らなければ kSizeClassCount）
        uint32_t SizeClassFor(uint32_t size)
        {
            uint32_t sizeClass = 0;
            while (sizeClass < ConstantBufferPool::kSizeClassCount && BlockBytes(sizeClass) < size) {
                ++sizeClass;
            }
            return sizeClass;
        }
    }

    ConstantBufferPool::~ConstantBufferPool()
    {
        Shutdown();
    }

    void ConstantBufferPool::Initialize(ID3D12Device* device)
    {
        assert(device && "ConstantBufferPool requires a device");
        std::lock_guard<std::mutex> lock(mutex_);
        device_ = device;
        shutdown_ = false;
    }

    void ConstantBufferPool::SetDeferredReleaseQueue(DeferredReleaseQueue* queue)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        deferredRelease_ = queue;
    }

    void ConstantBufferPool::Shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
        if (!pages_.empty()) {
            Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Buffer,
                "ConstantBufferPool終了: ページ {} 枚・貸し出し中 {} 個", pages_.size(), liveBlocks_);
        }
        for (Page& page : pages_) {
            if (page.resource && page.cpu) {
                page.resource->Unmap(0, nullptr);
            }
        }
        pages_.clear();
        for (auto& slots : freeSlots_) {
            slots.clear();
        }
        deferredRelease_ = nullptr;
        device_ = nullptr;
        liveBlocks_ = 0;
    }

    ConstantBufferBlock ConstantBufferPool::Allocate(uint32_t size)
    {
        const uint32_t sizeClass = SizeClassFor((std::max)(size, 1u));
        if (sizeClass >= kSizeClassCount) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "ConstantBufferPool: {} バイトは 1 ページ（{} バイト）に入りません", size, kPageBytes);
            return {};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!device_ || shutdown_) {
            return {};
        }

        std::vector<FreeSlot>& slots = freeSlots_[sizeClass];
        if (slots.empty() && !AppendPage(sizeClass)) {
            return {};
        }
        const FreeSlot free = slots.back();
        slots.pop_back();

        const Page& page = pages_[free.page];
        const uint32_t offset = free.slot * BlockBytes(sizeClass);

        ConstantBufferBlock block;
        block.cpu = page.cpu + offset;
        block.gpuAddress = page.gpuAddress + offset;
        block.page = free.page;
        block.slot = free.slot;
        block.sizeClass = sizeClass;
        std::memset(block.cpu, 0, BlockBytes(sizeClass));
        ++liveBlocks_;
        return block;
    }

    void ConstantBufferPool::Retire(ConstantBufferBlock& block)
    {
        if (!block.IsValid()) {
            return;
        }
        DeferredReleaseQueue* queue = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 終了処理の後はページごと消えているので、返す先が無い
            if (shutdown_) {
                block.Invalidate();
                return;
            }
            queue = deferredRelease_;
        }
        if (queue) {
            queue->PushForCurrentFrame(*this, block);
            return;
        }
        Reclaim(block);
        block.Invalidate();
    }

    void ConstantBufferPool::Reclaim(const ConstantBufferBlock& block)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_ || block.page >= pages_.size() || block.sizeClass >= kSizeClassCount) {
            return;
        }
        freeSlots_[block.sizeClass].push_back(FreeSlot{ block.page, block.slot });
        if (liveBlocks_ > 0) {
            --liveBlocks_;
        }
    }

    bool ConstantBufferPool::AppendPage(uint32_t sizeClass)
    {
        Page page;
        page.sizeClass = sizeClass;
        page.resource = ResourceFactory::CreateBufferResource(device_, kPageBytes);
        if (!page.resource) {
            return false;
        }
        page.resource->SetName(L"ConstantBufferPool.Page");
        void* mapped = nullptr;
        page.resource->Map(0, nullptr, &mapped);
        page.cpu = static_cast<uint8_t*>(mapped);
        page.gpuAddress = page.resource->GetGPUVirtualAddress();

        const uint32_t pageIndex = static_cast<uint32_t>(pages_.size());
        pages_.push_back(std::move(page));

        // 小さい番号から順に借りられるよう、後ろから積む
        const uint32_t blockCount = kPageBytes / BlockBytes(sizeClass);
        std::vector<FreeSlot>& slots = freeSlots_[sizeClass];
        slots.reserve(slots.size() + blockCount);
        for (uint32_t slot = blockCount; slot-- > 0;) {
            slots.push_back(FreeSlot{ pageIndex, slot });
        }
        return true;
    }

    size_t ConstantBufferPool::PageCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pages_.size();
    }

    size_t ConstantBufferPool::LiveBlockCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return liveBlocks_;
    }
}

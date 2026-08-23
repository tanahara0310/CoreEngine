#include "pch.h"
#include "Graphics/RHI/Resource/UploadRing.h"

#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>

namespace CoreEngine
{
    namespace
    {
        /// @brief value を alignment の倍数へ切り上げる（alignment は 2 のべき乗）
        constexpr uint32_t AlignUp(uint32_t value, uint32_t alignment)
        {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }
    }

    UploadRing::~UploadRing()
    {
        Shutdown();
    }

    void UploadRing::Initialize(ID3D12Device* device, uint32_t framesInFlight, uint32_t bytesPerFrame)
    {
        assert(device && "UploadRing requires a device");

        device_ = device;
        bytesPerPage_ = (std::max)(AlignUp(bytesPerFrame, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
            static_cast<uint32_t>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

        slots_.clear();
        slots_.resize((std::max)(1u, framesInFlight));
        currentSlot_ = 0;
        peakBytesPerFrame_ = 0;
        growCount_ = 0;

        // 全スロットの 1 枚目をここで確保しておく。
        // 遅延確保にすると「2 フレーム目で初めてページが増える」のが
        // 容量不足の警告と区別できなくなる（実際に一度出して紛らわしかった）。
        for (FrameSlot& slot : slots_) {
            AppendPage(slot, bytesPerPage_);
        }

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Buffer,
            "UploadRing初期化完了: {} スロット × {} KB", slots_.size(), bytesPerPage_ / 1024);
    }

    void UploadRing::Shutdown()
    {
        // 実際にどれだけ使ったかを残す。既定容量が妥当かはこの数字でしか判断できない
        if (!slots_.empty()) {
            peakBytesPerFrame_ = (std::max)(peakBytesPerFrame_, slots_[currentSlot_].bytesUsed);
            Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Buffer,
                "UploadRing終了: 1 フレームあたり最大 {} バイト使用（容量 {} KB・ページ追加 {} 回）",
                peakBytesPerFrame_, bytesPerPage_ / 1024, growCount_);
        }

        // Map は Release 時に暗黙で解除されるので Unmap は不要。
        // （明示的に Unmap してから Release してもよいが、二重管理になるので統一する）
        slots_.clear();
        device_ = nullptr;
    }

    bool UploadRing::AppendPage(FrameSlot& slot, uint32_t capacity)
    {
        if (!device_) {
            return false;
        }

        Page page;
        page.capacity = AlignUp(capacity, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        page.resource = ResourceFactory::CreateBufferResource(device_, page.capacity);
        if (!page.resource) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "UploadRing: ページ確保に失敗しました（{} KB）", page.capacity / 1024);
            return false;
        }

        void* mapped = nullptr;
        if (FAILED(page.resource->Map(0, nullptr, &mapped))) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "UploadRing: ページの Map に失敗しました");
            return false;
        }

        page.cpuBase = static_cast<uint8_t*>(mapped);
        page.gpuBase = page.resource->GetGPUVirtualAddress();
        page.resource->SetName(L"UploadRing::Page");

        slot.pages.push_back(std::move(page));
        return true;
    }

    void UploadRing::Reset(uint32_t frameIndex)
    {
        if (slots_.empty()) {
            return;
        }

        // 巻き戻す前に、直前のフレームがどれだけ使ったかを記録しておく
        peakBytesPerFrame_ = (std::max)(peakBytesPerFrame_, slots_[currentSlot_].bytesUsed);

        currentSlot_ = frameIndex % static_cast<uint32_t>(slots_.size());

        FrameSlot& slot = slots_[currentSlot_];
        slot.pageIndex = 0;
        slot.offset = 0;
        slot.bytesUsed = 0;
    }

    UploadAllocation UploadRing::Allocate(uint32_t size, uint32_t alignment)
    {
        if (!device_ || size == 0 || slots_.empty()) {
            return {};
        }
        if (alignment == 0) {
            alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        }

        FrameSlot& slot = slots_[currentSlot_];

        for (;;) {
            if (slot.pageIndex < slot.pages.size()) {
                Page& page = slot.pages[slot.pageIndex];
                const uint32_t start = AlignUp(slot.offset, alignment);

                if (start <= page.capacity && size <= page.capacity - start) {
                    slot.offset = start + size;
                    slot.bytesUsed += size;

                    UploadAllocation allocation;
                    allocation.cpu = page.cpuBase + start;
                    allocation.gpuAddress = page.gpuBase + start;
                    allocation.size = size;
                    return allocation;
                }

                // このページには入らない。次のページへ送る
                // （ページを跨いだ連続領域は作れないので、余りは捨てる）
                ++slot.pageIndex;
                slot.offset = 0;
                continue;
            }

            // ページが足りない。増やす（一度増えたら以後のフレームでは再利用される）。
            // 黙って切り詰めると「足りていないのに動いている」ように見えるので必ず記録する。
            const uint32_t capacity = (std::max)(bytesPerPage_, AlignUp(size, alignment));
            if (!AppendPage(slot, capacity)) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                    "UploadRing: {} バイトを確保できませんでした（この定数バッファは未設定のまま描画されます）", size);
                return {};
            }
            ++growCount_;
            // 1 枚目は Initialize が確保済みなので、ここへ来る＝1 フレームの確保量が
            // 1 ページを超えたということ。黙って増やすと「足りているように見える」ので記録する
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Buffer,
                "UploadRing: フレームスロット {} が 1 ページ（{} KB）を超えました。"
                "{} 枚目を追加します（今フレームの確保量 {} KB）",
                currentSlot_, bytesPerPage_ / 1024, slot.pages.size(), slot.bytesUsed / 1024);
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS UploadRing::AllocateConstants(const void* src, uint32_t size)
    {
        const UploadAllocation allocation = Allocate(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (!allocation.IsValid()) {
            return 0;
        }
        if (src) {
            std::memcpy(allocation.cpu, src, size);
        }
        return allocation.gpuAddress;
    }

    uint32_t UploadRing::BytesUsedThisFrame() const noexcept
    {
        return slots_.empty() ? 0u : slots_[currentSlot_].bytesUsed;
    }

    uint32_t UploadRing::CapacityPerFrame() const noexcept
    {
        if (slots_.empty()) {
            return 0;
        }
        uint32_t total = 0;
        for (const Page& page : slots_[currentSlot_].pages) {
            total += page.capacity;
        }
        return total;
    }
}

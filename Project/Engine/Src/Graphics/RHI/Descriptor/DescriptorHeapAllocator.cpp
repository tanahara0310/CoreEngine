#include "pch.h"
#include "Graphics/RHI/Descriptor/DescriptorHeapAllocator.h"

#include "Utility/Logger/Logger.h"

#include <cassert>
#include <format>
#include <stdexcept>

namespace CoreEngine
{
    namespace
    {
        /// @brief エンジンのヒープ種別 → D3D12 の種別とシェーダ可視性
        struct HeapTraits
        {
            D3D12_DESCRIPTOR_HEAP_TYPE d3dType;
            bool shaderVisible;
        };

        HeapTraits TraitsOf(DescriptorHeapType type)
        {
            switch (type) {
            case DescriptorHeapType::SRV_CBV_UAV: return { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true };
            case DescriptorHeapType::RTV:         return { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false };
            case DescriptorHeapType::DSV:         return { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false };
            default:
                assert(false && "Invalid descriptor heap type");
                return { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false };
            }
        }

        /// @brief 使用率がこれを超えたら 1 回だけ警告する
        constexpr float kHighUsageWarnRate = 0.80f;
    }

    void DescriptorHeapAllocator::Initialize(ID3D12Device* device, DescriptorHeapType type,
                                             uint32_t capacity, std::string_view debugName)
    {
        assert(device != nullptr && "Device must not be null");
        assert(capacity > 0 && "Descriptor heap capacity must be positive");

        device_ = device;
        type_ = type;
        capacity_ = capacity;
        debugName_ = std::string(debugName);

        const HeapTraits traits = TraitsOf(type);
        shaderVisible_ = traits.shaderVisible;

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = traits.d3dType;
        desc.NumDescriptors = capacity;
        desc.Flags = traits.shaderVisible
            ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
            : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        const HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap_.GetAddressOf()));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Heap,
                "ディスクリプタヒープの作成に失敗: {} (容量={}, hr=0x{:08X})\n",
                debugName_, capacity, static_cast<unsigned int>(hr));
            throw std::runtime_error("Failed to create descriptor heap: " + debugName_);
        }

        descriptorSize_ = device_->GetDescriptorHandleIncrementSize(traits.d3dType);
        cpuStart_ = heap_->GetCPUDescriptorHandleForHeapStart();
        gpuStart_ = shaderVisible_ ? heap_->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };

        nextIndex_ = 0;
        freeIndices_.clear();
        warnedHighUsage_ = false;
#ifdef _DEBUG
        allocated_.assign(capacity_, false);
#endif

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Heap,
            "{} ヒープ作成: 容量={}, 1スロット={}バイト, シェーダ可視={}\n",
            debugName_, capacity_, descriptorSize_, shaderVisible_ ? "はい" : "いいえ");
    }

    void DescriptorHeapAllocator::Shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        heap_.Reset();
        freeIndices_.clear();
#ifdef _DEBUG
        allocated_.clear();
#endif
        device_ = nullptr;
        nextIndex_ = 0;
    }

    DescriptorHandle DescriptorHeapAllocator::MakeHandle(uint32_t index) const
    {
        DescriptorHandle handle;
        handle.index = index;
        handle.heapType = type_;
        handle.cpuHandle.ptr = cpuStart_.ptr + static_cast<SIZE_T>(index) * descriptorSize_;
        handle.gpuHandle.ptr = shaderVisible_
            ? gpuStart_.ptr + static_cast<UINT64>(index) * descriptorSize_
            : 0; // RTV / DSV はシェーダ不可視
        return handle;
    }

    DescriptorHandle DescriptorHeapAllocator::Allocate(std::string_view debugName)
    {
        uint32_t index = 0;
        uint32_t live = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!freeIndices_.empty()) {
                index = freeIndices_.back();
                freeIndices_.pop_back();
            } else {
                if (nextIndex_ >= capacity_) {
                    Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Heap,
                        "{} ヒープが満杯です! 容量={} / 要求={} （用途: {}）\n",
                        debugName_, capacity_, nextIndex_, std::string(debugName));
                    throw std::runtime_error(debugName_ + " descriptor heap is full!");
                }
                index = nextIndex_++;
            }
#ifdef _DEBUG
            assert(!allocated_[index] && "同じスロットを二重に確保しました");
            allocated_[index] = true;
#endif
            live = nextIndex_ - static_cast<uint32_t>(freeIndices_.size());
        }

        // 使用率の警告は「初めて閾値を超えたとき」だけ出す。
        // 旧実装は確保のたびに INFO ログと警告を出しており、起動ログが埋まっていた。
        const float rate = static_cast<float>(live) / static_cast<float>(capacity_);
        if (!warnedHighUsage_ && rate > kHighUsageWarnRate) {
            warnedHighUsage_ = true;
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Heap,
                "{} ヒープの使用率が {:.1f}% に達しました（{}/{}）。容量の見直しを検討してください\n",
                debugName_, rate * 100.0f, live, capacity_);
        }

#ifdef _DEBUG
        Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Heap,
            "{}[{}] 確保: \"{}\"（使用 {}/{}）", debugName_, index, std::string(debugName), live, capacity_);
#else
        (void)debugName;
#endif

        return MakeHandle(index);
    }

    void DescriptorHeapAllocator::Free(DescriptorHandle& handle)
    {
        if (!handle.IsValid()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Heap,
                "{}: 無効なハンドルを解放しようとしました（すでに解放済みか未確保）\n", debugName_);
            return;
        }
        if (handle.heapType != type_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Heap,
                "{}: 別種別のハンドルを解放しようとしました（index={}）\n", debugName_, handle.index);
            assert(false && "ヒープ種別が一致しません");
            return;
        }

        const uint32_t index = handle.index;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            assert(index < capacity_ && "解放するインデックスが範囲外です");
#ifdef _DEBUG
            assert(allocated_[index] && "二重解放を検出しました");
            allocated_[index] = false;
#endif
            freeIndices_.push_back(index);
        }

#ifdef _DEBUG
        Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Heap,
            "{}[{}] 解放 — GPU フェンス完了後に呼ぶこと", debugName_, index);
#endif
        handle.Invalidate();
    }

    DescriptorHandle DescriptorHeapAllocator::HandleAt(uint32_t index) const
    {
        assert(index < capacity_ && "インデックスが範囲外です");
        return MakeHandle(index);
    }

    uint32_t DescriptorHeapAllocator::LiveCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextIndex_ - static_cast<uint32_t>(freeIndices_.size());
    }

    uint32_t DescriptorHeapAllocator::HighWaterMark() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextIndex_;
    }

    float DescriptorHeapAllocator::UsageRate() const
    {
        if (capacity_ == 0) {
            return 0.0f;
        }
        return static_cast<float>(LiveCount()) / static_cast<float>(capacity_);
    }
}

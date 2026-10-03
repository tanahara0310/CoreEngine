#include "pch.h"
#include "PerFrameStructuredBuffer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"

#include <cassert>
#include <cstring>
#include <utility>

namespace CoreEngine
{
    PerFrameBufferSlots::~PerFrameBufferSlots()
    {
        Release();
    }

    PerFrameBufferSlots::PerFrameBufferSlots(PerFrameBufferSlots&& other) noexcept
        : graphics_(std::exchange(other.graphics_, nullptr))
        , slots_(std::move(other.slots_))
        , capacityBytes_(std::exchange(other.capacityBytes_, 0))
        , uploadedGeneration_(std::exchange(other.uploadedGeneration_, 0))
    {
    }

    PerFrameBufferSlots& PerFrameBufferSlots::operator=(PerFrameBufferSlots&& other) noexcept
    {
        if (this != &other) {
            Release();
            graphics_ = std::exchange(other.graphics_, nullptr);
            slots_ = std::move(other.slots_);
            capacityBytes_ = std::exchange(other.capacityBytes_, 0);
            uploadedGeneration_ = std::exchange(other.uploadedGeneration_, 0);
        }
        return *this;
    }

    void PerFrameBufferSlots::Initialize(GraphicsCore& graphics, uint32_t stride, uint32_t capacity, const char* debugName)
    {
        Release();
        assert(stride > 0 && capacity > 0);

        graphics_ = &graphics;
        capacityBytes_ = static_cast<size_t>(stride) * capacity;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = capacity;
        srvDesc.Buffer.StructureByteStride = stride;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        // スロットは FrameIndex() が取りうる数だけ作る
        DescriptorAllocator& descriptors = *graphics.GetDescriptorAllocator();
        const uint32_t framesInFlight = graphics.Frame().FramesInFlight();
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            Slot& slot = slots_[i];
            slot.buffer = ResourceFactory::CreateBufferResource(graphics.GetDevice(), capacityBytes_);
            slot.buffer->Map(0, nullptr, reinterpret_cast<void**>(&slot.mapped));
            slot.srv = UniqueDescriptor(descriptors, descriptors.CreateSRV(slot.buffer.Get(), srvDesc, debugName));
        }
        uploadedGeneration_ = 0;
    }

    void PerFrameBufferSlots::Release()
    {
        if (!graphics_) {
            return;
        }
        for (Slot& slot : slots_) {
            slot.srv.Reset();
            slot.mapped = nullptr;
            if (slot.buffer) {
                graphics_->DeferRelease(std::move(slot.buffer));
            }
        }
        graphics_ = nullptr;
        capacityBytes_ = 0;
        uploadedGeneration_ = 0;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE PerFrameBufferSlots::Upload(const void* data, size_t bytes) const
    {
        if (!graphics_) {
            return {};
        }

        // 記録中のフレームのスロットは、GPU がそのスロットを使った前のフレームを終えてから回ってくる
        const Slot& slot = slots_[graphics_->Frame().FrameIndex()];
        const uint64_t generation = graphics_->GetUploadRing().Generation();
        if (uploadedGeneration_ != generation) {
            assert(bytes <= capacityBytes_);
            if (bytes > 0) {
                std::memcpy(slot.mapped, data, (std::min)(bytes, capacityBytes_));
            }
            uploadedGeneration_ = generation;
        }
        return slot.srv.Gpu();
    }
}

#include "pch.h"
#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"

#include <utility>

namespace CoreEngine
{
    UniqueDescriptor::UniqueDescriptor(UniqueDescriptor&& other) noexcept
        : allocator_(std::exchange(other.allocator_, nullptr))
        , handle_(std::exchange(other.handle_, DescriptorHandle{}))
    {
    }

    UniqueDescriptor& UniqueDescriptor::operator=(UniqueDescriptor&& other) noexcept
    {
        if (this != &other) {
            Reset();
            allocator_ = std::exchange(other.allocator_, nullptr);
            handle_ = std::exchange(other.handle_, DescriptorHandle{});
        }
        return *this;
    }

    void UniqueDescriptor::Reset()
    {
        if (allocator_ && handle_.IsValid()) {
            allocator_->Retire(handle_);
        }
        allocator_ = nullptr;
        handle_.Invalidate();
    }
}

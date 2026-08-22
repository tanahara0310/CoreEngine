#include "pch.h"
#include "Graphics/RHI/Command/CommandQueue.h"

#include "Utility/Logger/Logger.h"

#include <cassert>

namespace CoreEngine
{
    void CommandQueue::Initialize(ID3D12Device* device)
    {
        assert(device != nullptr && "Device must not be null");

        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        const HRESULT hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(queue_.GetAddressOf()));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "CommandQueue: 生成に失敗しました hr=0x{:08X}", static_cast<unsigned int>(hr));
            assert(false && "Failed to create the direct command queue");
            return;
        }
        queue_->SetName(L"MainDirectQueue");
    }

    void CommandQueue::Shutdown()
    {
        queue_.Reset();
    }

    void CommandQueue::Execute(ID3D12GraphicsCommandList* list)
    {
        if (!queue_ || !list) {
            return;
        }
        ID3D12CommandList* lists[] = { list };
        queue_->ExecuteCommandLists(1, lists);
    }
}

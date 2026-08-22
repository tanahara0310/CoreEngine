#include "pch.h"
#include "Graphics/RHI/Command/CommandContext.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>

namespace CoreEngine
{
    void CommandContext::Initialize(ID3D12Device* device, uint32_t framesInFlight)
    {
        assert(device != nullptr && "Device must not be null");
        framesInFlight_ = std::clamp(framesInFlight, 2u, kMaxFramesInFlight);

        for (uint32_t i = 0; i < framesInFlight_; ++i) {
            const HRESULT hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocators_[i].GetAddressOf()));
            assert(SUCCEEDED(hr) && "CommandContext: failed to create a command allocator");
            (void)hr;
        }

        const HRESULT hr = device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr,
            IID_PPV_ARGS(list_.GetAddressOf()));
        assert(SUCCEEDED(hr) && "CommandContext: failed to create the command list");
        (void)hr;
        list_->SetName(L"FrameCommandList");

        // 生成直後のリストは «記録中» のまま返す。フレーム 0 は Begin を通さずに
        // そのまま記録を始める（＝旧 CommandManager と同じ挙動）。

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Command,
            "CommandContext初期化完了: アロケータ{}本\n", framesInFlight_);
    }

    void CommandContext::Shutdown()
    {
        list_.Reset();
        for (auto& allocator : allocators_) {
            allocator.Reset();
        }
    }

    bool CommandContext::Close()
    {
        if (!list_) {
            return false;
        }
        const HRESULT hr = list_->Close();
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "CommandContext::Close 失敗 hr=0x{:08X}", static_cast<unsigned int>(hr));
            assert(false && "Failed to close the frame command list");
            return false;
        }
        return true;
    }

    void CommandContext::Begin(uint32_t frameIndex, ID3D12DescriptorHeap* srvHeap)
    {
        assert(frameIndex < framesInFlight_ && "frameIndex がフレーム数を超えています");
        if (!list_ || frameIndex >= framesInFlight_) {
            return;
        }

        ID3D12CommandAllocator* allocator = allocators_[frameIndex].Get();
        HRESULT hr = allocator->Reset();
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "CommandContext::Begin アロケータ Reset 失敗 hr=0x{:08X}", static_cast<unsigned int>(hr));
            assert(false && "Failed to reset the frame command allocator");
            return;
        }

        hr = list_->Reset(allocator, nullptr);
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "CommandContext::Begin リスト Reset 失敗 hr=0x{:08X}", static_cast<unsigned int>(hr));
            assert(false && "Failed to reset the frame command list");
            return;
        }

        // シェーダ可視ヒープはフレーム先頭で 1 回バインドすれば足りる
        // （各パスが個別に SetDescriptorHeaps を呼ぶ必要はない）
        BindDescriptorHeap(srvHeap);
    }

    void CommandContext::BindDescriptorHeap(ID3D12DescriptorHeap* srvHeap)
    {
        if (!list_ || !srvHeap) {
            return;
        }
        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        list_->SetDescriptorHeaps(1, heaps);
    }
}

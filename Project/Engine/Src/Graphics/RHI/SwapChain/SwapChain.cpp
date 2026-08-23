#include "pch.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <format>

using Microsoft::WRL::ComPtr;

namespace CoreEngine
{
    namespace
    {
        Logger& logger = Logger::GetInstance();
    }

    SwapChain::~SwapChain()
    {
        Shutdown();
    }

    bool SwapChain::Initialize(
        IDXGIFactory7* dxgiFactory,
        ID3D12CommandQueue* commandQueue,
        DescriptorAllocator* descriptorAllocator,
        const SwapChainDesc& desc)
    {
        assert(dxgiFactory && commandQueue && descriptorAllocator);
        assert(desc.hwnd && "SwapChain: hwnd が必要");
        assert(desc.bufferCount >= 2 && "SwapChain: Flip モデルはバッファ 2 枚以上");

        Shutdown();
        descriptorAllocator_ = descriptorAllocator;
        desc_ = desc;

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width = desc_.width;
        scDesc.Height = desc_.height;
        scDesc.Format = desc_.bufferFormat;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = desc_.bufferCount;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> swapChain1;
        HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
            commandQueue, desc_.hwnd, &scDesc, nullptr, nullptr, &swapChain1);
        if (FAILED(hr)) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "{}: CreateSwapChainForHwnd に失敗 {}x{} HRESULT={:#010x}\n",
                desc_.debugName, desc_.width, desc_.height, static_cast<unsigned>(hr));
            return false;
        }
        hr = swapChain1.As(&swapChain_);
        if (FAILED(hr)) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "{}: IDXGISwapChain4 への変換に失敗 HRESULT={:#010x}\n",
                desc_.debugName, static_cast<unsigned>(hr));
            swapChain_.Reset();
            return false;
        }

        if (!RetrieveBackBuffers()) {
            Shutdown();
            return false;
        }

        // RTV は 1 枚ごとに正規に確保する（Resize 時は同じスロットへ書き直す）。
        // 旧実装はヒープ先頭からの手計算（メイン）／専用ミニヒープ（第 2 ウィンドウ）だった。
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = desc_.rtvFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        rtvs_.resize(desc_.bufferCount);
        for (uint32_t i = 0; i < desc_.bufferCount; ++i) {
            rtvs_[i] = descriptorAllocator_->CreateRTV(
                backBuffers_[i].Get(), rtvDesc, std::format("{}_BackBuffer{}", desc_.debugName, i));
        }

        logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
            "{}: 生成完了 {}x{} buffers={} format={} rtvFormat={}\n",
            desc_.debugName, desc_.width, desc_.height, desc_.bufferCount,
            static_cast<int>(desc_.bufferFormat), static_cast<int>(desc_.rtvFormat));
        return true;
    }

    void SwapChain::Shutdown()
    {
        if (descriptorAllocator_) {
            // Free は未確保ハンドルを無視するので IsValid チェックは要らない
            for (DescriptorHandle& rtv : rtvs_) {
                descriptorAllocator_->Free(rtv);
            }
        }
        rtvs_.clear();
        backBuffers_.clear();
        swapChain_.Reset();
        descriptorAllocator_ = nullptr;
    }

    bool SwapChain::RetrieveBackBuffers()
    {
        backBuffers_.resize(desc_.bufferCount);
        for (uint32_t i = 0; i < desc_.bufferCount; ++i) {
            ComPtr<ID3D12Resource> buffer;
            const HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&buffer));
            if (FAILED(hr)) {
                logger.Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                    "{}: バックバッファ[{}] の取得に失敗 HRESULT={:#010x}\n",
                    desc_.debugName, i, static_cast<unsigned>(hr));
                return false;
            }
            // GetBuffer 直後のバックバッファは PRESENT（= COMMON）。ここが追跡の起点になる
            backBuffers_[i].Reset(std::move(buffer), D3D12_RESOURCE_STATE_PRESENT);
        }
        return true;
    }

    void SwapChain::ReleaseBackBuffers()
    {
        // ResizeBuffers はバックバッファへの参照が 1 つでも残っていると失敗する
        for (GpuResource& buffer : backBuffers_) {
            buffer.Release();
        }
    }

    void SwapChain::WriteRTVs()
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = desc_.rtvFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        for (uint32_t i = 0; i < desc_.bufferCount; ++i) {
            descriptorAllocator_->WriteRTV(rtvs_[i], backBuffers_[i].Get(), rtvDesc);
        }
    }

    bool SwapChain::Resize(uint32_t width, uint32_t height)
    {
        if (!swapChain_ || width == 0 || height == 0) {
            return false;
        }

        ReleaseBackBuffers();

        const HRESULT hr = swapChain_->ResizeBuffers(
            desc_.bufferCount, width, height, desc_.bufferFormat, 0);
        if (FAILED(hr)) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "{}: ResizeBuffers に失敗 {}x{} HRESULT={:#010x}\n",
                desc_.debugName, width, height, static_cast<unsigned>(hr));
            return false;
        }

        desc_.width = width;
        desc_.height = height;

        if (!RetrieveBackBuffers()) {
            return false;
        }
        WriteRTVs();

        logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
            "{}: リサイズ完了 {}x{}\n", desc_.debugName, width, height);
        return true;
    }

    HRESULT SwapChain::Present(UINT syncInterval, UINT flags)
    {
        if (!swapChain_) {
            return DXGI_ERROR_INVALID_CALL;
        }
        return swapChain_->Present(syncInterval, flags);
    }

    uint32_t SwapChain::CurrentBackBufferIndex() const
    {
        return swapChain_ ? swapChain_->GetCurrentBackBufferIndex() : 0u;
    }

    GpuResource& SwapChain::BackBuffer(uint32_t index)
    {
        assert(index < backBuffers_.size());
        return backBuffers_[index];
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::RTV(uint32_t index) const
    {
        assert(index < rtvs_.size());
        return rtvs_[index].cpuHandle;
    }
}

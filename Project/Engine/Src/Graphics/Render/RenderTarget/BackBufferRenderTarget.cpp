#include "BackBufferRenderTarget.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>

namespace CoreEngine
{
    void BackBufferRenderTarget::Initialize(DirectXCommon* dx)
    {
        assert(dx);
        dxCommon_ = dx;
        width_ = dx->GetClientWidth();
        height_ = dx->GetClientHeight();
    }

    void BackBufferRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(dxCommon_);

        UINT backBufferIndex = GetCurrentBackBufferIndex();
        ID3D12Resource* backBuffer = dxCommon_->GetSwapChainBackBuffer(backBufferIndex);

        // リソースバリア: PRESENT -> RENDER_TARGET
        TransitionBarrier(cmdList, backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // RTV & DSV設定
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetRTVHandle(backBufferIndex);
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDSVHandle();
        cmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

        // クリア
        cmdList->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
        cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // ビューポート設定
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &viewport);

        // シザー矩形設定
        D3D12_RECT scissor{};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = width_;
        scissor.bottom = height_;
        cmdList->RSSetScissorRects(1, &scissor);

        // SRVヒープ設定
        ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);
    }

    void BackBufferRenderTarget::End(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(dxCommon_);

        UINT backBufferIndex = GetCurrentBackBufferIndex();
        ID3D12Resource* backBuffer = dxCommon_->GetSwapChainBackBuffer(backBufferIndex);

        // リソースバリア: RENDER_TARGET -> PRESENT
        TransitionBarrier(cmdList, backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRenderTarget::GetRTVHandle() const
    {
        UINT index = GetCurrentBackBufferIndex();
        return dxCommon_->GetRTVHandle(index);
    }

    ID3D12Resource* BackBufferRenderTarget::GetResource() const
    {
        UINT index = GetCurrentBackBufferIndex();
        return dxCommon_->GetSwapChainBackBuffer(index);
    }

    void BackBufferRenderTarget::GetSize(int32_t& width, int32_t& height) const
    {
        width = width_;
        height = height_;
    }

    UINT BackBufferRenderTarget::GetCurrentBackBufferIndex() const
    {
        return dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
    }
}

#include "OffscreenRenderTarget.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>

namespace CoreEngine
{
    void OffscreenRenderTarget::Initialize(DirectXCommon* dx, int index)
    {
        assert(dx);
        assert(index >= 0);

        dxCommon_ = dx;
        index_ = index;

        dx->EnsureOffScreenTargetCount(static_cast<uint32_t>(index + 1));
        SyncCurrentState();
    }

    void OffscreenRenderTarget::SyncCurrentState() const
    {
        assert(dxCommon_);

        dxCommon_->EnsureOffScreenTargetCount(static_cast<uint32_t>(index_ + 1));

        resource_ = dxCommon_->GetOffScreenResource(static_cast<uint32_t>(index_));
        rtvHandle_ = dxCommon_->GetOffScreenRtvHandle(static_cast<uint32_t>(index_));
        srvHandle_ = dxCommon_->GetOffScreenSrvHandle(static_cast<uint32_t>(index_));
        dsvHandle_ = dxCommon_->GetDSVHandle();
        width_ = dxCommon_->GetClientWidth();
        height_ = dxCommon_->GetClientHeight();
    }

    void OffscreenRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        SyncCurrentState();
        assert(resource_);

        // リソースバリア: PIXEL_SHADER_RESOURCE -> RENDER_TARGET
        TransitionBarrier(cmdList, resource_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // RTV & DSV設定
        cmdList->OMSetRenderTargets(1, &rtvHandle_, false, &dsvHandle_);

        // クリア
        cmdList->ClearRenderTargetView(rtvHandle_, clearColor_, 0, nullptr);
        cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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

    void OffscreenRenderTarget::End(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        SyncCurrentState();
        assert(resource_);

        // リソースバリア: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
        TransitionBarrier(cmdList, resource_,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void OffscreenRenderTarget::GetSize(int32_t& width, int32_t& height) const
    {
        SyncCurrentState();
        width = width_;
        height = height_;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetRTVHandle() const
    {
        SyncCurrentState();
        return rtvHandle_;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetSRVHandle() const
    {
        SyncCurrentState();
        return srvHandle_;
    }

    ID3D12Resource* OffscreenRenderTarget::GetResource() const
    {
        SyncCurrentState();
        return resource_;
    }

    int32_t OffscreenRenderTarget::GetWidth() const
    {
        SyncCurrentState();
        return width_;
    }

    int32_t OffscreenRenderTarget::GetHeight() const
    {
        SyncCurrentState();
        return height_;
    }
}

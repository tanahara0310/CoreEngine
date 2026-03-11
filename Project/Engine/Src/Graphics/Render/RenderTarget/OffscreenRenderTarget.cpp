#include "OffscreenRenderTarget.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>

namespace CoreEngine
{
    void OffscreenRenderTarget::Initialize(DirectXCommon* dx, int index)
    {
        assert(dx);
        assert(index >= 0 && index <= 1);

        dxCommon_ = dx;
        index_ = index;

        // インデックスに応じてリソースを取得
        if (index == 0) {
            resource_ = dx->GetOffScreenResource();
            rtvHandle_ = dx->GetOffScreenRtvHandle();
            srvHandle_ = dx->GetOffScreenSrvHandle();
        } else {
            resource_ = dx->GetOffScreen2Resource();
            rtvHandle_ = dx->GetOffScreen2RtvHandle();
            srvHandle_ = dx->GetOffScreen2SrvHandle();
        }

        dsvHandle_ = dx->GetDSVHandle();
        width_ = dx->GetClientWidth();
        height_ = dx->GetClientHeight();
    }

    void OffscreenRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
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
        assert(resource_);

        // リソースバリア: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
        TransitionBarrier(cmdList, resource_,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void OffscreenRenderTarget::GetSize(int32_t& width, int32_t& height) const
    {
        width = width_;
        height = height_;
    }
}

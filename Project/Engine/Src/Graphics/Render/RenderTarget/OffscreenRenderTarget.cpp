#include "pch.h"
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
        uavHandle_ = dxCommon_->GetOffScreenUavHandle(static_cast<uint32_t>(index_));
        dsvHandle_ = useCustomDsvHandle_ ? customDsvHandle_ : dxCommon_->GetDSVHandle();
        width_ = dxCommon_->GetClientWidth();
        height_ = dxCommon_->GetClientHeight();
        currentState_ = dxCommon_->GetOffScreenState(static_cast<uint32_t>(index_));
    }

    void OffscreenRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        SyncCurrentState();
        assert(resource_);

        D3D12_RESOURCE_STATES& sharedState = dxCommon_->GetOffScreenStateRef(static_cast<uint32_t>(index_));

        // 実際のリソース状態から RENDER_TARGET へ遷移（状態不一致によるチラつきを防ぐ）
        if (sharedState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            TransitionBarrier(cmdList, resource_,
                sharedState,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        currentState_ = sharedState;

        // RTV & DSV設定
        // useDepthBuffer_=false の場合（SSAOなどポストプロセス専用パス）は
        // 共有DSVをバインドしない。これによりGBufferPassが書き込んだ深度値を保護する。
        if (useDepthBuffer_) {
            cmdList->OMSetRenderTargets(1, &rtvHandle_, false, &dsvHandle_);
        } else {
            cmdList->OMSetRenderTargets(1, &rtvHandle_, false, nullptr);
        }

        // clearEnabled_=true のときのみRTVと深度をクリアする。
        // clearEnabled_=false は DeferredLightingPass が書き込んだ結果を
        // GeometryPass が上書きする際など、直前パスの内容を保持したい場合に使用する。
        if (clearEnabled_) {
            cmdList->ClearRenderTargetView(rtvHandle_, clearColor_, 0, nullptr);
            if (useDepthBuffer_) {
                cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            }
        }

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

        D3D12_RESOURCE_STATES& sharedState = dxCommon_->GetOffScreenStateRef(static_cast<uint32_t>(index_));

        // 実際のリソース状態から PIXEL_SHADER_RESOURCE へ遷移
        if (sharedState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            TransitionBarrier(cmdList, resource_,
                sharedState,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        currentState_ = sharedState;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetUAVHandle() const
    {
        SyncCurrentState();
        return uavHandle_;
    }

    void OffscreenRenderTarget::BeginCS(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        SyncCurrentState();
        assert(resource_);

        D3D12_RESOURCE_STATES& sharedState = dxCommon_->GetOffScreenStateRef(static_cast<uint32_t>(index_));

        if (sharedState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            TransitionBarrier(cmdList, resource_,
                sharedState,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        currentState_ = sharedState;

        // SRVヒープ設定（CS用バインドに必要）
        ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);
    }

    void OffscreenRenderTarget::EndCS(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        SyncCurrentState();
        assert(resource_);

        D3D12_RESOURCE_STATES& sharedState = dxCommon_->GetOffScreenStateRef(static_cast<uint32_t>(index_));

        if (sharedState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            TransitionBarrier(cmdList, resource_,
                sharedState,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        currentState_ = sharedState;
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

    void OffscreenRenderTarget::SetCurrentState(D3D12_RESOURCE_STATES state)
    {
        currentState_ = state;
        if (dxCommon_) {
            dxCommon_->SetOffScreenState(static_cast<uint32_t>(index_), state);
        }
    }

    D3D12_RESOURCE_STATES& OffscreenRenderTarget::GetCurrentState()
    {
        assert(dxCommon_);
        currentState_ = dxCommon_->GetOffScreenState(static_cast<uint32_t>(index_));
        return dxCommon_->GetOffScreenStateRef(static_cast<uint32_t>(index_));
    }

    D3D12_RESOURCE_STATES OffscreenRenderTarget::GetCurrentState() const
    {
        SyncCurrentState();
        return currentState_;
    }
}

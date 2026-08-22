#include "pch.h"
#include "BackBufferRenderTarget.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"
#include <cassert>

namespace CoreEngine
{
    void BackBufferRenderTarget::Initialize(GraphicsCore* dx)
    {
        assert(dx);
        dxCommon_ = dx;
        currentState_ = D3D12_RESOURCE_STATE_PRESENT;
    }

    void BackBufferRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(dxCommon_);

        UINT backBufferIndex = GetCurrentBackBufferIndex();
        ID3D12Resource* backBuffer = dxCommon_->GetSwapChainBackBuffer(backBufferIndex);

        // 現在状態を基準にして RENDER_TARGET へ遷移する。
        if (currentState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            ResourceBarrierHelper::Transition(cmdList, backBuffer,
                currentState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        // 最終合成はフルスクリーン描画のみで深度を使用しないため RTV のみ設定する。
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetRTVHandle(backBufferIndex);
        cmdList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

        // バックバッファのみクリアする。
        cmdList->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);

        // ビューポート設定（キャッシュせず現在のクライアント領域サイズを都度取得する）
        const int32_t width = dxCommon_->GetClientWidth();
        const int32_t height = dxCommon_->GetClientHeight();
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &viewport);

        // シザー矩形設定
        D3D12_RECT scissor{};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = width;
        scissor.bottom = height;
        cmdList->RSSetScissorRects(1, &scissor);

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）
    }

    void BackBufferRenderTarget::End(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(dxCommon_);

        UINT backBufferIndex = GetCurrentBackBufferIndex();
        ID3D12Resource* backBuffer = dxCommon_->GetSwapChainBackBuffer(backBufferIndex);

        // 描画完了後に Present 可能状態へ戻す。
        if (currentState_ != D3D12_RESOURCE_STATE_PRESENT) {
            ResourceBarrierHelper::Transition(cmdList, backBuffer,
                currentState_,
                D3D12_RESOURCE_STATE_PRESENT);
            currentState_ = D3D12_RESOURCE_STATE_PRESENT;
        }
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
        width = dxCommon_->GetClientWidth();
        height = dxCommon_->GetClientHeight();
    }

    int32_t BackBufferRenderTarget::GetWidth() const
    {
        return dxCommon_->GetClientWidth();
    }

    int32_t BackBufferRenderTarget::GetHeight() const
    {
        return dxCommon_->GetClientHeight();
    }

    UINT BackBufferRenderTarget::GetCurrentBackBufferIndex() const
    {
        return dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
    }
}

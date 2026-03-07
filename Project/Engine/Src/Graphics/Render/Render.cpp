#include "Render.h"
#include "Graphics/Common/DirectXCommon.h"

using namespace Microsoft::WRL;


namespace CoreEngine
{
    void Render::Initialize(DirectXCommon* dxCommon, ComPtr<ID3D12DescriptorHeap> dsvHeap)
    {
        dxCommon_ = dxCommon;
        dsvHeap_ = dsvHeap;

        // ディスクリプラサイズ取得
        dsvSize_ = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // 1枚目のオフスクリーン情報を取得
        offscreenRtvHandle_ = dxCommon->GetOffScreenRtvHandle();
        offscreenResource_ = dxCommon->GetOffScreenResource();

        // 2枚目のオフスクリーン情報を取得
        offscreen2RtvHandle_ = dxCommon->GetOffScreen2RtvHandle();
        offscreen2Resource_ = dxCommon->GetOffScreen2Resource();

        UpdateViewportAndScissor(dxCommon_->GetClientWidth(), dxCommon_->GetClientHeight());
    }

    // 描画前処理（1枚目のオフスクリーン）
    void Render::OffscreenPreDraw(int offscreenIndex)
    {
        UpdateViewportAndScissor(dxCommon_->GetClientWidth(), dxCommon_->GetClientHeight());

        offscreenRtvHandle_ = dxCommon_->GetOffScreenRtvHandle();
        offscreenResource_ = dxCommon_->GetOffScreenResource();
        offscreen2RtvHandle_ = dxCommon_->GetOffScreen2RtvHandle();
        offscreen2Resource_ = dxCommon_->GetOffScreen2Resource();

        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};

        // インデックスに基づいてリソースとハンドルを選択
        switch (offscreenIndex) {
        case 0:
            resource = offscreenResource_;
            rtvHandle = offscreenRtvHandle_;
            break;
        case 1:
            resource = offscreen2Resource_;
            rtvHandle = offscreen2RtvHandle_;
            break;
        default:
            assert(false && "Invalid offscreen index");
            return;
        }

        OffscreenPreDrawCommon(resource, rtvHandle);
    }

    // オフスクリーンの描画前処理（汎用）
    void Render::OffscreenPreDrawCommon(ID3D12Resource* resource, const D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle)
    {
        auto* cmdList = dxCommon_->GetCommandList();

        // オフスクリーンリソースをRENDER_TARGETに移行
        ResourceBarrier(resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        // RTV & DSV設定 - DirectXCommonからDSVハンドルを直接取得
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDSVHandle();
        cmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

        // 統一クリアカラーを使用
        cmdList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

        // 深度バッファをクリアする
        cmdList->ClearDepthStencilView(
            dsvHandle, // DSVのハンドル
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f, // 深度値のクリア値
            0, // ステンシル値のクリア値
            0, // フラグ
            nullptr); // Rect

        // 描画用のDescriptorHeapを設定
        ID3D12DescriptorHeap* descriptorHeaps[] = {
            dxCommon_->GetSRVHeap(), // SRVヒープを設定
        };
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);

        // Viewportを設定
        cmdList->RSSetViewports(1, &viewport_);

        // Scissorを設定
        cmdList->RSSetScissorRects(1, &scissorRect_);
    }

    void Render::BackBufferPreDraw()
    {
        UpdateViewportAndScissor(dxCommon_->GetClientWidth(), dxCommon_->GetClientHeight());

        auto* cmdList = dxCommon_->GetCommandList();
        UINT backBufferIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
        ID3D12Resource* backBuffer = dxCommon_->GetSwapChainBackBuffer(backBufferIndex);

        // バックバッファをRENDER_TARGETに移行
        ResourceBarrier(backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // バックバッファを描画先にセットしてクリア
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetRTVHandle(backBufferIndex);
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDSVHandle();

        cmdList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
        // 指定した色で画面全体をクリアする（統一クリアカラーを使用）
        cmdList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
        // 深度バッファをクリアする
        cmdList->ClearDepthStencilView(
            dsvHandle, // DSVのハンドル
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f, // 深度値のクリア値
            0, // ステンシル値のクリア値
            0, // フラグ
            nullptr);

        // 描画用のDescriptorHeapを設定
        ID3D12DescriptorHeap* descriptorHeaps[] = {
            dxCommon_->GetSRVHeap(), // SRVヒープを設定
        };
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);
        // Viewportを設定
        cmdList->RSSetViewports(1, &viewport_);
        // Scissorを設定
        cmdList->RSSetScissorRects(1, &scissorRect_);
    }

    // 描画後処理（1枚目のオフスクリーン）
    void Render::OffscreenPostDraw(int offscreenIndex)
    {
        ID3D12Resource* resource = nullptr;

        // インデックスに基づいてリソースを選択
        switch (offscreenIndex) {
        case 0:
            resource = offscreenResource_;
            break;
        case 1:
            resource = offscreen2Resource_;
            break;
        default:
            assert(false && "Invalid offscreen index");
            return;
        }

        OffscreenPostDrawCommon(resource);
    }

    // オフスクリーンの描画後処理（汎用）
    void Render::OffscreenPostDrawCommon(ID3D12Resource* resource)
    {
        // オフスクリーンに書き込んだ状態からシェーダーで読める状態へ遷移
        ResourceBarrier(resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void Render::BackBufferPostDraw()
    {
        // バックバッファの描画後処理
        auto* cmdList = dxCommon_->GetCommandList();
        UINT backBufferIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
        ID3D12Resource* backBuffer = dxCommon_->GetSwapChainBackBuffer(backBufferIndex);

        // バックバッファをPRESENTに移行
        ResourceBarrier(backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

        // コマンドリストをClose
        HRESULT hr = cmdList->Close();
        assert(SUCCEEDED(hr));

        // コマンドを実行
        ID3D12CommandList* commandLists[] = { cmdList };
        dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

        // 現在のフレーム完了をシグナル（非ブロッキング）
        auto* commandManager = dxCommon_->GetCommandManager();
        if (commandManager) {
            commandManager->SignalFrame(backBufferIndex);
        }

        // Present（画面に反映）
        // VSyncを有効化して60FPS固定
        static constexpr UINT kVSyncEnabled = 1;  // 1 = VSyncを待つ（60Hz）、0 = 即座に描画
        static constexpr UINT kPresentFlags = 0;  // Present時の追加フラグ
        dxCommon_->GetSwapChain()->Present(kVSyncEnabled, kPresentFlags);

        // 次のフレームの準備
        UINT nextFrameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

        // 次のフレームのGPU処理が完了するまで待機（ダブルバッファリング)
        if (commandManager) {
            commandManager->WaitForFrame(nextFrameIndex);
        }

        // 次のフレーム用のコマンドアロケータをリセット
        hr = commandManager->GetCommandAllocator(nextFrameIndex)->Reset();
        assert(SUCCEEDED(hr));

        // コマンドリストをリセット
        hr = dxCommon_->GetCommandList()->Reset(commandManager->GetCommandAllocator(nextFrameIndex), nullptr);
        assert(SUCCEEDED(hr));
    }

    void Render::ResourceBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;

    dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
}

D3D12_CPU_DESCRIPTOR_HANDLE Render::GetOffscreenRTVHandle(int offscreenIndex) const
{
    return (offscreenIndex == 0) ? offscreenRtvHandle_ : offscreen2RtvHandle_;
}

D3D12_CPU_DESCRIPTOR_HANDLE Render::GetDSVHandle() const
{
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

void Render::UpdateViewportAndScissor(int32_t width, int32_t height)
{
    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.right = width;
    scissorRect_.top = 0;
    scissorRect_.bottom = height;
}
}

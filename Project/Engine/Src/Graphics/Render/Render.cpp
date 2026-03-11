#include "Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"

using namespace Microsoft::WRL;


namespace CoreEngine
{
    void Render::Initialize(DirectXCommon* dxCommon, ComPtr<ID3D12DescriptorHeap> dsvHeap)
    {
        dxCommon_ = dxCommon;
        dsvHeap_ = dsvHeap;

        // RenderTargetManagerの初期化
        renderTargetManager_ = std::make_unique<RenderTargetManager>();
        renderTargetManager_->Initialize(dxCommon, dsvHeap);

        // デフォルトのレンダーターゲットを作成（名前ベース）
        RenderTargetDescriptor offscreen0Desc("Offscreen0");
        offscreen0Desc.clearColor[0] = kClearColor[0];
        offscreen0Desc.clearColor[1] = kClearColor[1];
        offscreen0Desc.clearColor[2] = kClearColor[2];
        offscreen0Desc.clearColor[3] = kClearColor[3];
        renderTargetManager_->CreateRenderTarget(offscreen0Desc);

        RenderTargetDescriptor offscreen1Desc("Offscreen1");
        offscreen1Desc.clearColor[0] = kClearColor[0];
        offscreen1Desc.clearColor[1] = kClearColor[1];
        offscreen1Desc.clearColor[2] = kClearColor[2];
        offscreen1Desc.clearColor[3] = kClearColor[3];
        renderTargetManager_->CreateRenderTarget(offscreen1Desc);

        // バックバッファターゲットを作成
        renderTargetManager_->CreateBackBufferTarget("BackBuffer");
    }

    RenderTarget* Render::GetRenderTarget(const std::string& name)
    {
        return renderTargetManager_->GetRenderTarget(name);
    }

    void Render::FinalizeFrame()
    {
        auto* cmdList = dxCommon_->GetCommandList();
        UINT backBufferIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

        // バックバッファの終了処理
        auto* backBuffer = renderTargetManager_->GetRenderTarget("BackBuffer");
        if (backBuffer) {
            backBuffer->End(cmdList);
        }

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
        static constexpr UINT kVSyncEnabled = 1;
        static constexpr UINT kPresentFlags = 0;
        dxCommon_->GetSwapChain()->Present(kVSyncEnabled, kPresentFlags);

        // 次のフレームの準備
        UINT nextFrameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

        // 次のフレームのGPU処理が完了するまで待機
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
}

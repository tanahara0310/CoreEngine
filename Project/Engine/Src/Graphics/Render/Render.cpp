#include "Render.h"
#include "Graphics/Common/DirectXCommon.h"

using namespace Microsoft::WRL;


namespace CoreEngine
{
    void Render::Initialize(DirectXCommon* dxCommon, ComPtr<ID3D12DescriptorHeap> dsvHeap)
    {
        dxCommon_ = dxCommon;
        dsvHeap_ = dsvHeap;

        // RenderTargetの初期化
        for (int i = 0; i < kOffscreenCount; ++i) {
            offscreenTargets_[i] = std::make_unique<OffscreenRenderTarget>();
            offscreenTargets_[i]->Initialize(dxCommon, i);
            offscreenTargets_[i]->SetClearColor(kClearColor);
        }

        backBufferTarget_ = std::make_unique<BackBufferRenderTarget>();
        backBufferTarget_->Initialize(dxCommon);
        backBufferTarget_->SetClearColor(kClearColor);
    }

    RenderTarget* Render::GetOffscreenTarget(int index)
    {
        if (index < 0 || index >= kOffscreenCount) {
            return nullptr;
        }
        return offscreenTargets_[index].get();
    }

    RenderTarget* Render::GetBackBufferTarget()
    {
        return backBufferTarget_.get();
    }

    void Render::FinalizeFrame()
    {
        auto* cmdList = dxCommon_->GetCommandList();
        UINT backBufferIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

        // バックバッファの終了処理
        backBufferTarget_->End(cmdList);

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

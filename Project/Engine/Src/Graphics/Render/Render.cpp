#include "pch.h"
#include "Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"

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

        // デフォルトのレンダーターゲットを作成
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

        RenderTargetDescriptor sceneViewDesc("SceneView");
        sceneViewDesc.clearColor[0] = kClearColor[0];
        sceneViewDesc.clearColor[1] = kClearColor[1];
        sceneViewDesc.clearColor[2] = kClearColor[2];
        sceneViewDesc.clearColor[3] = kClearColor[3];
        renderTargetManager_->CreateRenderTarget(sceneViewDesc);

        // SSAO用バッファ
        // フルスクリーンポストプロセスのためDSVは不要（深度バッファを破壊しないようにする）
        // クリア色は白（AO無し = 1.0）。リソース作成時と ClearRenderTargetView 時を一致させる
        static constexpr float kSSAOClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        RenderTargetDescriptor ssaoDesc("SSAOBuffer");
        ssaoDesc.clearColor[0] = kSSAOClearColor[0];
        ssaoDesc.clearColor[1] = kSSAOClearColor[1];
        ssaoDesc.clearColor[2] = kSSAOClearColor[2];
        ssaoDesc.clearColor[3] = kSSAOClearColor[3];
        ssaoDesc.needsDepthStencil = false;
        if (auto* ssaoTarget = renderTargetManager_->CreateRenderTarget(ssaoDesc)) {
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(ssaoTarget)) {
                offscreen->SetUseDepthBuffer(false);
                // リソース作成時のクリア色を白に設定して不一致を解消
                auto* dx = renderTargetManager_->GetDirectXCommon();
                dx->SetOffScreenTargetClearColor(static_cast<uint32_t>(offscreen->GetIndex()), kSSAOClearColor);
            }
        }

        RenderTargetDescriptor ssaoBlurDesc("SSAOBlurBuffer");
        ssaoBlurDesc.clearColor[0] = kSSAOClearColor[0];
        ssaoBlurDesc.clearColor[1] = kSSAOClearColor[1];
        ssaoBlurDesc.clearColor[2] = kSSAOClearColor[2];
        ssaoBlurDesc.clearColor[3] = kSSAOClearColor[3];
        ssaoBlurDesc.needsDepthStencil = false;
        if (auto* ssaoBlurTarget = renderTargetManager_->CreateRenderTarget(ssaoBlurDesc)) {
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(ssaoBlurTarget)) {
                offscreen->SetUseDepthBuffer(false);
                auto* dx = renderTargetManager_->GetDirectXCommon();
                dx->SetOffScreenTargetClearColor(static_cast<uint32_t>(offscreen->GetIndex()), kSSAOClearColor);
            }
        }

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

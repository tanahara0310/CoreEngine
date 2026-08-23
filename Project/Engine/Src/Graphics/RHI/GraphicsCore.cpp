#include "pch.h"
#include "Graphics/RHI/GraphicsCore.h"

#include "Graphics/RHI/Device/DeviceManager.h"
#include "Graphics/RHI/Command/CommandQueue.h"
#include "Graphics/RHI/Command/CommandContext.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/SwapChain/SwapChainManager.h"
#include "Graphics/RHI/Resource/DepthStencilManager.h"
#include "Graphics/RHI/IResizable.h"

#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"
#include "EngineSystem/EngineConfig.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace CoreEngine
{
    // ================================================================
    // ライフサイクル
    // ================================================================

    GraphicsCore::GraphicsCore()
        : deviceManager_(std::make_unique<DeviceManager>())
        , commandQueue_(std::make_unique<CommandQueue>())
        , frameSync_(std::make_unique<FrameSync>())
        , commandContext_(std::make_unique<CommandContext>())
        , deferredRelease_(std::make_unique<DeferredReleaseQueue>())
        , descriptorAllocator_(std::make_unique<DescriptorAllocator>())
        , swapChainManager_(std::make_unique<SwapChainManager>())
        , depthStencilManager_(std::make_unique<DepthStencilManager>())
        , uploadContext_(std::make_unique<UploadContext>())
    {
    }

    GraphicsCore::~GraphicsCore()
    {
        Shutdown();
    }

    void GraphicsCore::Initialize(WinApp* winApp, const EngineConfig& config)
    {
        // ウィンドウズアプリケーション管理
        winApp_ = winApp;

        // 初期化順序を守って各管理クラスを初期化
        deviceManager_->Initialize(config.enableDebugLayer, config.enableGPUBasedValidation);

        ID3D12Device* device = deviceManager_->GetDevice();
        commandQueue_->Initialize(device);
        frameSync_->Initialize(device, commandQueue_->Get(), config.frameCount);
        commandContext_->Initialize(device, frameSync_->FramesInFlight());

        descriptorAllocator_->Initialize(device,
            config.maxSRVDescriptors, config.maxRTVDescriptors, config.maxDSVDescriptors);

        // フレーム 0 のコマンドリストは EndFrame を経ずにそのまま記録が始まるので、
        // ここでシェーダ可視ヒープをバインドしておく。
        // （これが無いと最初のフレームだけディスクリプタヒープ未設定で描画される）
        commandContext_->BindDescriptorHeap(descriptorAllocator_->GetSRVHeap());

        // アップロード／オフライン生成用の独立コンテキスト。
        // キューはフレームと共有（submit 順 = 実行順を保つため）だが、
        // アロケータ・コマンドリスト・フェンスは完全に別物。
        uploadContext_->Initialize(device, commandQueue_->Get());

        // スワップチェーンの初期化（バックバッファ取得とRTV作成まで含む）
        swapChainManager_->Initialize(
            device,
            deviceManager_->GetDXGIFactory(),
            commandQueue_->Get(),
            descriptorAllocator_.get(),
            winApp_->GetHwnd(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // 深度ステンシルの初期化（DescriptorManagerを渡す）
        depthStencilManager_->Initialize(
            device,
            descriptorAllocator_.get(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // ウィンドウリサイズ時のコールバックを設定
        winApp_->SetResizeCallback([this](int32_t width, int32_t height) {
            OnWindowResize(width, height);
            });
    }

    void GraphicsCore::Shutdown()
    {
        if (!frameSync_) {
            return;
        }
        // 全GPUコマンドの完了を待ってからリソースを解放する
        frameSync_->WaitForGpuIdle();
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "GraphicsCore::Shutdown: GPU同期完了。全マネージャーを解放します\n");

        // unique_ptr を明示的にリセットして破棄順序を制御する
        // （デストラクタ任せにすると宣言逆順になるため意図を明示）
        // UploadContext / DeferredReleaseQueue はコマンドキューの作業に紐づくため先に落とす。
        if (uploadContext_) {
            uploadContext_->Shutdown();
        }
        uploadContext_.reset();
        if (deferredRelease_) {
            deferredRelease_->ReleaseAll(); // GPU 待ち済みなのでここで捨ててよい
        }
        deferredRelease_.reset();
        depthStencilManager_.reset();
        swapChainManager_.reset();
        descriptorAllocator_.reset();
        if (commandContext_) {
            commandContext_->Shutdown();
        }
        commandContext_.reset();
        frameSync_->Shutdown();
        frameSync_.reset();
        if (commandQueue_) {
            commandQueue_->Shutdown();
        }
        commandQueue_.reset();
        deviceManager_.reset();
    }

    void GraphicsCore::OnWindowResize(int32_t width, int32_t height)
    {
        // コマンドの実行を待つ（リソースが使用中でないことを保証）
        frameSync_->WaitForGpuIdle();

        // GPU が空になったので、解放待ちのリソースはここで確実に落とせる
        deferredRelease_->Collect(frameSync_->CompletedValue());

        // スワップチェーンのリサイズ
        swapChainManager_->Resize(width, height);

        // 深度ステンシルのリサイズ（DSVハンドルは再利用）
        depthStencilManager_->ResizeResource(width, height);

        for (IResizable* resizable : resizables_) {
            if (resizable) {
                resizable->OnWindowResize(width, height);
            }
        }

        Logger::GetInstance().Log(
            L"Window Resized: " + std::to_wstring(width) + L"x" + std::to_wstring(height),
            LogLevel::INFO,
            LogCategory::Graphics);
    }

    void GraphicsCore::RegisterResizable(IResizable* resizable)
    {
        resizables_.push_back(resizable);
    }

    // ================================================================
    // フレームのライフサイクル
    // ================================================================

    FrameContext GraphicsCore::BeginFrame()
    {
        frameSync_->BeginFrame();

        // 前フレームまでに解放予約されたものを回収する。
        // GPU が到達済みのフェンス値だけを見るのでブロックしない。
        const size_t released = deferredRelease_->Collect(frameSync_->CompletedValue());
#ifdef _DEBUG
        if (released > 0) {
            Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Command,
                "DeferredRelease: {} 件解放（残 {} 件・completedFence={}）",
                released, deferredRelease_->PendingCount(), frameSync_->CompletedValue());
        }
#else
        (void)released;
#endif

        // アップロードコンテキストの中間バッファを回収する（GPU 完了済みのぶんだけ）。
        // これを呼ばないと UPLOAD ヒープ（システムメモリ）が解放されず溜まり続ける。
        uploadContext_->ReleaseCompletedResources();

        FrameContext frame;
        frame.frameIndex = frameSync_->FrameIndex();
        frame.frameNumber = frameSync_->FrameNumber();
        frame.cmdList = commandContext_->List();
        return frame;
    }

    void GraphicsCore::EndFrame(UINT syncInterval)
    {
        // 記録終了 → 投入
        if (!commandContext_->Close()) {
            return;
        }
        commandQueue_->Execute(commandContext_->List());

        // 現在のフレームの完了をシグナル（非ブロッキング）
        frameSync_->SignalCurrentFrame();

        // Present（画面に反映）
        static constexpr UINT kPresentFlags = 0;
        swapChainManager_->GetSwapChain()->Present(syncInterval, kPresentFlags);

        // ── 次フレームの準備 ──────────────────────────────────
        // ここで Reset まで済ませることで、フレーム外（Update 中など）でも
        // コマンドリストが常に記録可能な状態に保たれる。
        // アロケータ／フェンスのローテーションは FrameSync が持つスロット番号を使う。
        // スワップチェーンの GetCurrentBackBufferIndex() は ResizeBuffers で 0 に
        // リセットされるため、アロケータ選択に使うと実行中アロケータを Reset して
        // しまう（D3D12 ERROR #552）。
        frameSync_->AdvanceToNextFrame();
        commandContext_->Begin(frameSync_->FrameIndex(), descriptorAllocator_->GetSRVHeap());
    }

    FrameSync& GraphicsCore::Frame() const { return *frameSync_; }
    DeferredReleaseQueue& GraphicsCore::DeferredRelease() const { return *deferredRelease_; }

    // ================================================================
    // アクセッサ
    // ================================================================
    // ヘッダを軽く保つために実装をここへ置いている（inline にはしない）。
    // 呼び出し頻度はフレームあたり数十回程度で、ホットループではない。

    ID3D12Device* GraphicsCore::GetDevice() const { return deviceManager_->GetDevice(); }
    IDXGIFactory7* GraphicsCore::GetDXGIFactory() const { return deviceManager_->GetDXGIFactory(); }

    ID3D12CommandQueue* GraphicsCore::GetCommandQueue() const { return commandQueue_->Get(); }
    ID3D12GraphicsCommandList* GraphicsCore::GetCommandList() const { return commandContext_->List(); }
    UploadContext* GraphicsCore::GetUploadContext() const { return uploadContext_.get(); }
    void GraphicsCore::WaitForGpuIdle() { frameSync_->WaitForGpuIdle(); }

    IDXGISwapChain4* GraphicsCore::GetSwapChain() const { return swapChainManager_->GetSwapChain(); }
    ID3D12Resource* GraphicsCore::GetSwapChainBackBuffer(UINT index) const { return swapChainManager_->GetSwapChainBackBuffer(index); }
    GpuResource& GraphicsCore::GetBackBufferResource(UINT index) const { return swapChainManager_->BackBuffer(index); }
    D3D12_RENDER_TARGET_VIEW_DESC GraphicsCore::GetRTVDesc() const { return swapChainManager_->GetRTVDesc(); }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GraphicsCore::GetRTVHandle(UINT index) const { return swapChainManager_->GetRTVHandle(index); }

    DescriptorAllocator* GraphicsCore::GetDescriptorAllocator() const { return descriptorAllocator_.get(); }
    ID3D12DescriptorHeap* GraphicsCore::GetSRVHeap() const { return descriptorAllocator_->GetSRVHeap(); }
    ID3D12DescriptorHeap* GraphicsCore::GetDSVHeap() const { return descriptorAllocator_->GetDSVHeap(); }

    DepthStencilManager* GraphicsCore::GetDepthStencilManager() const { return depthStencilManager_.get(); }
    ID3D12Resource* GraphicsCore::GetDepthStencilResource() const { return depthStencilManager_->GetDepthStencilResource(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GraphicsCore::GetDSVHandle() const { return depthStencilManager_->GetDSVHandle(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GraphicsCore::GetDepthStencilSRV() const { return depthStencilManager_->GetDepthSRVHandle(); }

    int32_t GraphicsCore::GetClientWidth() const { return winApp_ ? winApp_->GetClientWidth() : WinApp::kClientWidth; }
    int32_t GraphicsCore::GetClientHeight() const { return winApp_ ? winApp_->GetClientHeight() : WinApp::kClientHeight; }
}

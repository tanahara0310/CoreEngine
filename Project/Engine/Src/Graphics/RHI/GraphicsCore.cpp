#include "pch.h"
#include "Graphics/RHI/GraphicsCore.h"

#include "Graphics/RHI/GraphicsCoreDesc.h"
#include "Graphics/RHI/Device/DeviceManager.h"
#include "Graphics/RHI/Command/CommandQueue.h"
#include "Graphics/RHI/Command/CommandContext.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Device/DeviceRemovedHandler.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"
#include "Graphics/RHI/IResizable.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <stdexcept>

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
        , swapChain_(std::make_unique<SwapChain>())
        , uploadContext_(std::make_unique<UploadContext>())
        , uploadRing_(std::make_unique<UploadRing>())
    {
    }

    GraphicsCore::~GraphicsCore()
    {
        Shutdown();
    }

    void GraphicsCore::Initialize(const GraphicsCoreDesc& desc)
    {
        clientWidth_ = desc.clientWidth;
        clientHeight_ = desc.clientHeight;

        // 初期化順序を守って各管理クラスを初期化
        deviceManager_->Initialize(desc.enableDebugLayer, desc.enableGPUBasedValidation, desc.enableDRED);

        ID3D12Device* device = deviceManager_->GetDevice();
        commandQueue_->Initialize(device);
        frameSync_->Initialize(device, commandQueue_->Get(), desc.framesInFlight);
        commandContext_->Initialize(device, frameSync_->FramesInFlight());

        descriptorAllocator_->Initialize(device,
            desc.maxSRVDescriptors, desc.maxRTVDescriptors, desc.maxDSVDescriptors);

        // フレーム 0 は EndFrame を経ずに記録が始まるので、ここでシェーダ可視ヒープをバインドする
        commandContext_->BindDescriptorHeap(descriptorAllocator_->GetSRVHeap());

        // アップロード／オフライン生成用の独立コンテキスト（キューだけフレームと共有）
        uploadContext_->Initialize(device, commandQueue_->Get());

        // フレーム内で使い捨てる定数バッファ置き場。スロット数はフレーム数と一致させる。
        // 巻き戻しはコマンドアロケータの Reset と同じ場所で行う（EndFrame 末尾）。
        uploadRing_->Initialize(device, frameSync_->FramesInFlight());
        uploadRing_->Reset(frameSync_->FrameIndex());

        // メインウィンドウのスワップチェーン（バックバッファ取得と RTV 作成まで含む）
        SwapChainDesc swapChainDesc{};
        swapChainDesc.hwnd = desc.hwnd;
        swapChainDesc.width = static_cast<uint32_t>(desc.clientWidth);
        swapChainDesc.height = static_cast<uint32_t>(desc.clientHeight);
        swapChainDesc.debugName = "MainSwapChain";
        if (!swapChain_->Initialize(
                deviceManager_->GetDXGIFactory(),
                commandQueue_->Get(),
                descriptorAllocator_.get(),
                swapChainDesc)) {
            throw std::runtime_error("GraphicsCore: failed to create the main swap chain");
        }
    }

    void GraphicsCore::Shutdown()
    {
        if (!frameSync_) {
            return;
        }
        // 全 GPU コマンドの完了を待ってから解放する。デバイスロスト時も解放は最後まで進める
        try {
            frameSync_->WaitForGpuIdle();
        } catch (const std::exception& e) {
            Logger::GetInstance().Errorf(LogCategory::Graphics,
                "GraphicsCore::Shutdown: GPU 待ちに失敗しましたが解放を続行します: {}", e.what());
        }
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "GraphicsCore::Shutdown: GPU同期完了。全マネージャーを解放します\n");

        // 破棄順序を明示する。UploadContext / DeferredReleaseQueue はキューに紐づくので先に落とす
        if (uploadContext_) {
            uploadContext_->Shutdown();
        }
        uploadContext_.reset();
        uploadRing_.reset();
        if (deferredRelease_) {
            deferredRelease_->ReleaseAll(); // GPU 待ち済みなのでここで捨ててよい
        }
        deferredRelease_.reset();
        // スワップチェーンは RTV を DescriptorAllocator へ返すので、アロケータより先に落とす
        if (swapChain_) {
            swapChain_->Shutdown();
        }
        swapChain_.reset();
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
        // 通知先が GetClientWidth() を読むので、先に更新しておく
        clientWidth_ = width;
        clientHeight_ = height;

        // コマンドの実行を待つ（リソースが使用中でないことを保証）
        frameSync_->WaitForGpuIdle();

        // GPU が空になったので、解放待ちのリソースはここで確実に落とせる
        deferredRelease_->Collect(frameSync_->CompletedValue());

        // メインスワップチェーンのリサイズ（RTV は同じスロットへ書き直される）
        if (!swapChain_->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
            throw std::runtime_error("GraphicsCore: failed to resize the main swap chain");
        }

        // 登録順に通知する（シーン深度 / GBuffer → レンダーターゲット群 の順で作り直される）。
        // 通知先が自分を解除してもよいよう、コピーを回す
        const std::vector<IResizable*> targets = resizables_;
        for (IResizable* resizable : targets) {
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
        if (!resizable) {
            return;
        }
        if (std::find(resizables_.begin(), resizables_.end(), resizable) != resizables_.end()) {
            return; // 二重登録すると同じオブジェクトが 2 回リサイズされる
        }
        resizables_.push_back(resizable);
    }

    void GraphicsCore::UnregisterResizable(IResizable* resizable)
    {
        resizables_.erase(
            std::remove(resizables_.begin(), resizables_.end(), resizable),
            resizables_.end());
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

        // Present。GPU クラッシュを最初に報告してくる場所なので戻り値を捨てない
        static constexpr UINT kPresentFlags = 0;
        const HRESULT presentResult = swapChain_->Present(syncInterval, kPresentFlags);
        if (FAILED(presentResult)) {
            if (ReportIfDeviceRemoved(deviceManager_->GetDevice(), "GraphicsCore::EndFrame（Present）")) {
                throw std::runtime_error(
                    "GPU device removed at Present (詳細は Graphics ログを参照)");
            }
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "Present に失敗しました hr=0x{:08X}（デバイスは生きています）",
                static_cast<uint32_t>(presentResult));
        }

        // ── 次フレームの準備 ──────────────────────────────────
        // ここで Reset まで済ませ、フレーム外でもコマンドリストを記録可能に保つ。
        // スロット番号は FrameSync のものを使う（CurrentBackBufferIndex() は ResizeBuffers で 0 に戻る）
        frameSync_->AdvanceToNextFrame();
        commandContext_->Begin(frameSync_->FrameIndex(), descriptorAllocator_->GetSRVHeap());
        // 定数リングの巻き戻しはコマンドアロケータの Reset と同じ条件（このスロットの
        // GPU 完了待ちが済んでいること）で成立する。2 つを別の場所に書くと片方だけずれる。
        uploadRing_->Reset(frameSync_->FrameIndex());
    }

    FrameSync& GraphicsCore::Frame() const { return *frameSync_; }
    DeferredReleaseQueue& GraphicsCore::DeferredRelease() const { return *deferredRelease_; }

    // ================================================================
    // アクセッサ（ヘッダを軽く保つため実装はここに置く）
    // ================================================================

    ID3D12Device* GraphicsCore::GetDevice() const { return deviceManager_->GetDevice(); }
    IDXGIFactory7* GraphicsCore::GetDXGIFactory() const { return deviceManager_->GetDXGIFactory(); }

    ID3D12CommandQueue* GraphicsCore::GetCommandQueue() const { return commandQueue_->Get(); }
    ID3D12GraphicsCommandList* GraphicsCore::GetCommandList() const { return commandContext_->List(); }
    UploadContext* GraphicsCore::GetUploadContext() const { return uploadContext_.get(); }
    UploadRing& GraphicsCore::GetUploadRing() const { return *uploadRing_; }
    void GraphicsCore::WaitForGpuIdle() { frameSync_->WaitForGpuIdle(); }

    SwapChain& GraphicsCore::GetSwapChain() const { return *swapChain_; }

    DescriptorAllocator* GraphicsCore::GetDescriptorAllocator() const { return descriptorAllocator_.get(); }
    ID3D12DescriptorHeap* GraphicsCore::GetSRVHeap() const { return descriptorAllocator_->GetSRVHeap(); }
}

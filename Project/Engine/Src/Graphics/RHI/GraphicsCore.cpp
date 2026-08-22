#include "pch.h"
#include "Graphics/RHI/GraphicsCore.h"

#include "Graphics/RHI/Device/DeviceManager.h"
#include "Graphics/RHI/Command/CommandManager.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/RHI/Descriptor/DescriptorManager.h"
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
        , commandManager_(std::make_unique<CommandManager>())
        , descriptorManager_(std::make_unique<DescriptorManager>())
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
        commandManager_->Initialize(deviceManager_->GetDevice(), config.frameCount);
        descriptorManager_->Initialize(deviceManager_->GetDevice(),
            config.maxSRVDescriptors, config.maxRTVDescriptors, config.maxDSVDescriptors);

        // アップロード／オフライン生成用の独立コンテキスト。
        // キューはフレームと共有（submit 順 = 実行順を保つため）だが、
        // アロケータ・コマンドリスト・フェンスは完全に別物。
        uploadContext_->Initialize(deviceManager_->GetDevice(), commandManager_->GetCommandQueue());

        // スワップチェーンの初期化（バックバッファ取得とRTV作成まで含む）
        swapChainManager_->Initialize(
            deviceManager_->GetDevice(),
            deviceManager_->GetDXGIFactory(),
            commandManager_->GetCommandQueue(),
            descriptorManager_.get(),
            winApp_->GetHwnd(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // 深度ステンシルの初期化（DescriptorManagerを渡す）
        depthStencilManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // ウィンドウリサイズ時のコールバックを設定
        winApp_->SetResizeCallback([this](int32_t width, int32_t height) {
            OnWindowResize(width, height);
            });
    }

    void GraphicsCore::Shutdown()
    {
        if (!commandManager_) {
            return;
        }
        // 全GPUコマンドの完了を待ってからリソースを解放する
        commandManager_->WaitForGpuIdle();
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "GraphicsCore::Shutdown: GPU同期完了。全マネージャーを解放します\n");

        // unique_ptr を明示的にリセットして破棄順序を制御する
        // （デストラクタ任せにすると宣言逆順になるため意図を明示）
        // UploadContext はコマンドキューを参照しているため CommandManager より先に落とす。
        if (uploadContext_) {
            uploadContext_->Shutdown();
        }
        uploadContext_.reset();
        depthStencilManager_.reset();
        swapChainManager_.reset();
        descriptorManager_.reset();
        commandManager_.reset();
        deviceManager_.reset();
    }

    void GraphicsCore::OnWindowResize(int32_t width, int32_t height)
    {
        // コマンドの実行を待つ（リソースが使用中でないことを保証）
        commandManager_->WaitForGpuIdle();

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
    // アクセッサ
    // ================================================================
    // ヘッダを軽く保つために実装をここへ置いている（inline にはしない）。
    // 呼び出し頻度はフレームあたり数十回程度で、ホットループではない。

    ID3D12Device* GraphicsCore::GetDevice() const { return deviceManager_->GetDevice(); }
    IDXGIFactory7* GraphicsCore::GetDXGIFactory() const { return deviceManager_->GetDXGIFactory(); }

    ID3D12CommandQueue* GraphicsCore::GetCommandQueue() const { return commandManager_->GetCommandQueue(); }
    ID3D12GraphicsCommandList* GraphicsCore::GetCommandList() const { return commandManager_->GetCommandList(); }
    CommandManager* GraphicsCore::GetCommandManager() const { return commandManager_.get(); }
    UploadContext* GraphicsCore::GetUploadContext() const { return uploadContext_.get(); }
    void GraphicsCore::WaitForGpuIdle() { commandManager_->WaitForGpuIdle(); }

    IDXGISwapChain4* GraphicsCore::GetSwapChain() const { return swapChainManager_->GetSwapChain(); }
    ID3D12Resource* GraphicsCore::GetSwapChainBackBuffer(UINT index) const { return swapChainManager_->GetSwapChainBackBuffer(index); }
    D3D12_RENDER_TARGET_VIEW_DESC GraphicsCore::GetRTVDesc() const { return swapChainManager_->GetRTVDesc(); }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GraphicsCore::GetRTVHandle(UINT index) const { return swapChainManager_->GetRTVHandle(index); }

    DescriptorManager* GraphicsCore::GetDescriptorManager() const { return descriptorManager_.get(); }
    ID3D12DescriptorHeap* GraphicsCore::GetSRVHeap() const { return descriptorManager_->GetSRVHeap(); }
    ID3D12DescriptorHeap* GraphicsCore::GetDSVHeap() const { return descriptorManager_->GetDSVHeap(); }

    DepthStencilManager* GraphicsCore::GetDepthStencilManager() const { return depthStencilManager_.get(); }
    ID3D12Resource* GraphicsCore::GetDepthStencilResource() const { return depthStencilManager_->GetDepthStencilResource(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GraphicsCore::GetDSVHandle() const { return depthStencilManager_->GetDSVHandle(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GraphicsCore::GetDepthStencilSRV() const { return depthStencilManager_->GetDepthSRVHandle(); }

    int32_t GraphicsCore::GetClientWidth() const { return winApp_ ? winApp_->GetClientWidth() : WinApp::kClientWidth; }
    int32_t GraphicsCore::GetClientHeight() const { return winApp_ ? winApp_->GetClientHeight() : WinApp::kClientHeight; }
}

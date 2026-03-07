#include "DirectXCommon.h"
#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// DirectXの初期化

namespace CoreEngine
{
    void DirectXCommon::Initialize(WinApp* winApp)
    {
        // ウィンドウズアプリケーション管理
        winApp_ = winApp;

        // 初期化順序を守って各管理クラスを初期化
        deviceManager_->Initialize(winApp);
        commandManager_->Initialize(deviceManager_->GetDevice());
        descriptorManager_->Initialize(deviceManager_->GetDevice());

        // スワップチェーンの初期化（バックバッファ取得とRTV作成まで含む）
        swapChainManager_->Initialize(
            deviceManager_->GetDevice(),
            deviceManager_->GetDXGIFactory(),
            commandManager_->GetCommandQueue(),
            descriptorManager_->GetRTVHeap(),
            winApp);

        // オフスクリーンレンダリングターゲットの作成
        offScreenManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // 深度ステンシルの初期化（DescriptorManagerを渡す）
        depthStencilManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // シャドウマップの初期化
        shadowMapManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get());

        // ウィンドウリサイズ時のコールバックを設定
        winApp_->SetResizeCallback([this](int32_t width, int32_t height) {
            OnWindowResize(width, height);
            });
    }

    // ウィンドウリサイズ時の処理
    void DirectXCommon::OnWindowResize(int32_t width, int32_t height)
    {

        // コマンドの実行を待つ（リソースが使用中でないことを保証）
        commandManager_->WaitForPreviousFrame();

        // スワップチェーンのリサイズ
        swapChainManager_->Resize(width, height);

        // 深度ステンシルのリサイズ（DSVハンドルは再利用）
        depthStencilManager_->ResizeResource(width, height);

        // オフスクリーンレンダリングターゲットのリサイズ
        offScreenManager_->Resize(width, height);

        Logger::GetInstance().Log(L"Window Resized: " + std::to_wstring(width) + L"x" + std::to_wstring(height),
            LogLevel::INFO, LogCategory::Graphics);
    }
}

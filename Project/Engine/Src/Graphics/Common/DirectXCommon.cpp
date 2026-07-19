#include "pch.h"
#include "DirectXCommon.h"
#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"
#include "EngineSystem/EngineConfig.h"
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace CoreEngine
{
    void DirectXCommon::Shutdown() {
        if (!commandManager_) {
            return;
        }
        // 全GPUコマンドの完了を待ってからリソースを解放する
        commandManager_->WaitForPreviousFrame();
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "DirectXCommon::Shutdown: GPU同期完了。全マネージャーを解放します\n");

        // unique_ptr を明示的にリセットして破棄順序を制御する
        // （デストラクタ任せにすると宣言逆順になるため意図を明示）
        depthStencilManager_.reset();
        swapChainManager_.reset();
        descriptorManager_.reset();
        commandManager_.reset();
        deviceManager_.reset();
    }

    DirectXCommon::~DirectXCommon() {
        Shutdown();
    }

    void DirectXCommon::Initialize(WinApp* winApp, const EngineConfig& config)
    {
        // ウィンドウズアプリケーション管理
        winApp_ = winApp;

        // 初期化順序を守って各管理クラスを初期化
        deviceManager_->Initialize(winApp, config.enableDebugLayer, config.enableGPUBasedValidation);
        commandManager_->Initialize(deviceManager_->GetDevice(), config.frameCount);
        descriptorManager_->Initialize(deviceManager_->GetDevice(),
            config.maxSRVDescriptors, config.maxRTVDescriptors, config.maxDSVDescriptors);

        // スワップチェーンの初期化（バックバッファ取得とRTV作成まで含む）
        swapChainManager_->Initialize(
            deviceManager_->GetDevice(),
            deviceManager_->GetDXGIFactory(),
            commandManager_->GetCommandQueue(),
            descriptorManager_.get(),
            winApp);

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




    // ウィンドウリサイズ時の処理
    void DirectXCommon::OnWindowResize(int32_t width, int32_t height)
    {

        // コマンドの実行を待つ（リソースが使用中でないことを保証）
        commandManager_->WaitForPreviousFrame();

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
}



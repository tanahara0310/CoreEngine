#include "pch.h"
#include "DirectXCommon.h"
#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"
#include "EngineSystem/EngineConfig.h"
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// DirectXの初期化

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
        rtShadowManager_.reset();
        accelerationStructureManager_.reset();
        shadowMapManager_.reset();
        gBufferManager_.reset();
        offScreenManager_.reset();
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
        commandManager_->Initialize(deviceManager_->GetDevice());
        descriptorManager_->Initialize(deviceManager_->GetDevice(),
            config.maxSRVDescriptors, config.maxRTVDescriptors, config.maxDSVDescriptors);

        // スワップチェーンの初期化（バックバッファ取得とRTV作成まで含む）
        swapChainManager_->Initialize(
            deviceManager_->GetDevice(),
            deviceManager_->GetDXGIFactory(),
            commandManager_->GetCommandQueue(),
            descriptorManager_.get(),
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

        // G-Bufferの初期化
        gBufferManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get(),
            winApp_->GetClientWidth(),
            winApp_->GetClientHeight());

        // シャドウマップの初期化
        shadowMapManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get());

        // 加速構造マネージャーの初期化（DXR 非対応の場合は内部でスキップ）
        accelerationStructureManager_->Initialize(
            deviceManager_->GetDevice(),
            descriptorManager_.get());

        // レイトレーシングシャドウマネージャーの初期化
        if (accelerationStructureManager_->IsSupported()) {
            rtShadowManager_->Initialize(this, descriptorManager_.get(),
                accelerationStructureManager_.get());
        }

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

        // G-Bufferのリサイズ
        gBufferManager_->Resize(width, height);

        Logger::GetInstance().Log(
            L"Window Resized: " + std::to_wstring(width) + L"x" + std::to_wstring(height),
            LogLevel::INFO,
            LogCategory::Graphics);
    }
}



#pragma once

#include <cassert>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <memory>
#include <vector>

#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"

// 分離された管理クラス
#include "Graphics/Common/Core/DeviceManager.h"
#include "Graphics/Common/Core/CommandManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/Core/SwapChainManager.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Common/Core/UploadContext.h"
#include "Graphics/Common/IResizable.h"

using namespace Microsoft::WRL;

/// @brief DirectX12の初期化・管理を行うメインクラス

namespace CoreEngine
{

struct EngineConfig;

class DirectXCommon {
public:
    /// @brief 初期化
    /// @param winApp ウィンドウアプリケーション
    /// @param config エンジン設定
    void Initialize(WinApp* winApp, const EngineConfig& config);

    /// @brief シャットダウン処理（GPU完了待ちの後、全マネージャーを解放）
    void Shutdown();

    /// @brief デストラクタ（Shutdownが未呼び出しの場合も安全にGPU待ちする）
    ~DirectXCommon();

    /// @brief ウィンドウリサイズ時の処理
    /// @param width 新しい幅
    /// @param height 新しい高さ
    void OnWindowResize(int32_t width, int32_t height);

    /// @brief ウィンドウリサイズ通知を受け取るクラスを登録する
    /// @details スワップチェーン/深度バッファの再作成が終わった後に OnWindowResize() が呼ばれる。
    ///          呼び出し元は登録したオブジェクトを DirectXCommon より先に破棄してはならない
    ///          （生存期間の管理は呼び出し元の責務。エンジン構成上、常に DirectXCommon より
    ///          先に破棄されることはない）
    /// @param resizable 登録するインスタンス
    void RegisterResizable(IResizable* resizable) { resizables_.push_back(resizable); }

    // デバイス関連のアクセッサ
    ID3D12Device* GetDevice() { return deviceManager_->GetDevice(); }
    IDXGIFactory7* GetDXGIFactory() { return deviceManager_->GetDXGIFactory(); }

    // コマンド関連のアクセッサ
    ID3D12CommandQueue* GetCommandQueue() { return commandManager_->GetCommandQueue(); }
    ID3D12CommandAllocator* GetCommandAllocator() { return commandManager_->GetCommandAllocator(); }
    ID3D12GraphicsCommandList* GetCommandList() { return commandManager_->GetCommandList(); }
    CommandManager* GetCommandManager() { return commandManager_.get(); } // CommandManager自体へのアクセス

    /// @brief フレーム描画とは独立したアップロード／オフライン生成用コンテキストを取得
    /// @details テクスチャ・VB/IB のアップロードや IBL 生成は必ずこちらへ積むこと。
    ///          GetCommandList()（＝フレームの描画用リスト）へ積むと、ワーカースレッドからの
    ///          記録がメインスレッドの描画記録と競合し、フレーム外の Close/Execute が
    ///          記録途中のフレームを巻き添えで submit する。
    UploadContext* GetUploadContext() { return uploadContext_.get(); }

    // スワップチェーン関連のアクセッサ
    IDXGISwapChain4* GetSwapChain() { return swapChainManager_->GetSwapChain(); }
    ID3D12Resource* GetSwapChainBackBuffer(UINT index) { return swapChainManager_->GetSwapChainBackBuffer(index); }
    D3D12_RENDER_TARGET_VIEW_DESC GetRTVDesc() const { return swapChainManager_->GetRTVDesc(); }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTVHandle(UINT index) { return swapChainManager_->GetRTVHandle(index); }

    // ディスクリプタ関連のアクセッサ
    ID3D12DescriptorHeap* GetRTVHeap() { return descriptorManager_->GetRTVHeap(); }
    ID3D12DescriptorHeap* GetSRVHeap() { return descriptorManager_->GetSRVHeap(); }
    ID3D12DescriptorHeap* GetDSVHeap() { return descriptorManager_->GetDSVHeap(); }

    // 深度ステンシル関連のアクセッサ
    ID3D12Resource* GetDepthStencilResource() { return depthStencilManager_->GetDepthStencilResource(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() { return depthStencilManager_->GetDSVHandle(); }
    /// @brief 深度テクスチャの SRV GPU ハンドルを返す（水面 Depth Fade 等で使用）
    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthStencilSRV() { return depthStencilManager_->GetDepthSRVHandle(); }

    // マネージャーへの直接アクセス（必要に応じて）
    DescriptorManager* GetDescriptorManager() { return descriptorManager_.get(); }
    DepthStencilManager* GetDepthStencilManager() { return depthStencilManager_.get(); }
    
    // DXR対応状況のアクセッサ
    bool IsDXRSupported() const { return deviceManager_->IsDXRSupported(); }
    D3D12_RAYTRACING_TIER GetDXRTier() const { return deviceManager_->GetDXRTier(); }
    /// @brief 投入済みの全 GPU 作業の完了を待つ（完全同期）
    /// @details リソースの作り直し・破棄の直前にだけ使うこと。
    void WaitForGpuIdle() { commandManager_->WaitForGpuIdle(); }

    int32_t GetClientWidth() const { return winApp_ ? winApp_->GetClientWidth() : WinApp::kClientWidth; }
    int32_t GetClientHeight() const { return winApp_ ? winApp_->GetClientHeight() : WinApp::kClientHeight; }

private:
    // ウィンドウズアプリケーション管理
    WinApp* winApp_ = nullptr;

    Logger& logger = Logger::GetInstance();

    //管理クラス
    std::unique_ptr<DeviceManager> deviceManager_ = std::make_unique<DeviceManager>();
    std::unique_ptr<CommandManager> commandManager_ = std::make_unique<CommandManager>();
    std::unique_ptr<DescriptorManager> descriptorManager_ = std::make_unique<DescriptorManager>();
    std::unique_ptr<SwapChainManager> swapChainManager_ = std::make_unique<SwapChainManager>();
    std::unique_ptr<DepthStencilManager> depthStencilManager_ = std::make_unique<DepthStencilManager>();
    std::unique_ptr<UploadContext> uploadContext_ = std::make_unique<UploadContext>();

    std::vector<IResizable*> resizables_;
};
}

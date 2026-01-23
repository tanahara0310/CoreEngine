#pragma once

#include <cassert>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <memory>

#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"

// 分離された管理クラス
#include "Graphics/Common/Core/DeviceManager.h"
#include "Graphics/Common/Core/CommandManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/Core/SwapChainManager.h"
#include "Graphics/Common/Core/OffScreenRenderTargetManager.h"
#include "Graphics/Common/Core/DepthStencilManager.h"

using namespace Microsoft::WRL;

/// @brief DirectX12の初期化・管理を行うメインクラス

namespace CoreEngine
{
class DirectXCommon {
public:
	/// @brief 初期化
	/// @param winApp ウィンドウアプリケーション
	void Initialize(WinApp* winApp);

	/// @brief ウィンドウリサイズ時の処理
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void OnWindowResize(int32_t width, int32_t height);

	// デバイス関連のアクセッサ
	ID3D12Device* GetDevice() { return deviceManager_->GetDevice(); }
	IDXGIFactory7* GetDXGIFactory() { return deviceManager_->GetDXGIFactory(); }

	// コマンド関連のアクセッサ
	ID3D12CommandQueue* GetCommandQueue() { return commandManager_->GetCommandQueue(); }
	ID3D12CommandAllocator* GetCommandAllocator() { return commandManager_->GetCommandAllocator(); }
	ID3D12GraphicsCommandList* GetCommandList() { return commandManager_->GetCommandList(); }
	CommandManager* GetCommandManager() { return commandManager_.get(); } // CommandManager自体へのアクセス

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

	// マネージャーへの直接アクセス（必要に応じて）
	DescriptorManager* GetDescriptorManager() { return descriptorManager_.get(); }
	DepthStencilManager* GetDepthStencilManager() { return depthStencilManager_.get(); }

	// オフスクリーン用のアクセッサ（1枚目）
	ID3D12Resource* GetOffScreenResource() { return offScreenManager_->GetOffScreenResource(); }
	D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreenRtvHandle() { return offScreenManager_->GetOffScreenRtvHandle(); }
	D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreenSrvHandle() { return offScreenManager_->GetOffScreenSrvHandle(); }

	// オフスクリーン用のアクセッサ（2枚目）
	ID3D12Resource* GetOffScreen2Resource() { return offScreenManager_->GetOffScreen2Resource(); }
	D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreen2RtvHandle() { return offScreenManager_->GetOffScreen2RtvHandle(); }
	D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreen2SrvHandle() { return offScreenManager_->GetOffScreen2SrvHandle(); }

	// フェンスを待機
	void WaitForPreviousFrame() { commandManager_->WaitForPreviousFrame(); }

private:
	// ウィンドウズアプリケーション管理
	WinApp* winApp_ = nullptr;

	Logger& logger = Logger::GetInstance();

	//管理クラス
	std::unique_ptr<DeviceManager> deviceManager_ = std::make_unique<DeviceManager>();
	std::unique_ptr<CommandManager> commandManager_ = std::make_unique<CommandManager>();
	std::unique_ptr<DescriptorManager> descriptorManager_ = std::make_unique<DescriptorManager>();
	std::unique_ptr<SwapChainManager> swapChainManager_ = std::make_unique<SwapChainManager>();
	std::unique_ptr<OffScreenRenderTargetManager> offScreenManager_ = std::make_unique<OffScreenRenderTargetManager>();
	std::unique_ptr<DepthStencilManager> depthStencilManager_ = std::make_unique<DepthStencilManager>();
};
}

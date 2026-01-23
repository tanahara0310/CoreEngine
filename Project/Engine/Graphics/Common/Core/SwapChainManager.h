#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <cstdint>

// 前方宣言
namespace CoreEngine {
	class WinApp;
}

namespace CoreEngine
{
/// @brief スワップチェーン管理クラス
class SwapChainManager {
public:
	/// @brief 初期化
	/// @param device D3D12デバイス
	/// @param dxgiFactory DXGIファクトリ
	/// @param commandQueue コマンドキュー
	/// @param rtvHeap RTVディスクリプタヒープ
	/// @param winApp ウィンドウアプリケーション
	void Initialize(ID3D12Device* device, IDXGIFactory7* dxgiFactory, ID3D12CommandQueue* commandQueue,
		ID3D12DescriptorHeap* rtvHeap, CoreEngine::WinApp* winApp);

	/// @brief スワップチェーンのリサイズ
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void Resize(std::int32_t width, std::int32_t height);

	// アクセッサ
	IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
	ID3D12Resource* GetSwapChainBackBuffer(UINT index) const { return swapChainResources_[index].Get(); }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTVHandle(UINT index) const { return rtvHandles_[index]; }
	D3D12_RENDER_TARGET_VIEW_DESC GetRTVDesc() const { return rtvDesc_; }

private:
	/// @brief スワップチェーンの生成
	void CreateSwapChain();

	/// @brief スワップチェーンのバックバッファを取得
	void RetrieveBackBuffers();

	/// @brief RTVを作成
	void CreateRTVs();

private:
	// スワップチェーン関連
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2];

	// バックバッファのリソース
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[2];
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_;

	// 依存関係
	IDXGIFactory7* dxgiFactory_ = nullptr;
	ID3D12CommandQueue* commandQueue_ = nullptr;
	CoreEngine::WinApp* winApp_ = nullptr;

	// RTVヒープの参照（リサイズ時のRTV再作成用）
	ID3D12DescriptorHeap* rtvHeap_ = nullptr;
	ID3D12Device* device_ = nullptr;

	// 初回初期化フラグ
	bool isInitialized_ = false;
};
}

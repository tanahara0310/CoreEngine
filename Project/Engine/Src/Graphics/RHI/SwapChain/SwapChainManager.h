#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <cstdint>

// 前方宣言
namespace CoreEngine {
    class DescriptorManager;
}

namespace CoreEngine
{
/// @brief スワップチェーン管理クラス
/// @note WinApp には依存しない（HWND とサイズを値で受け取る）。
class SwapChainManager {
public:
    /// @brief 初期化
    /// @param descriptorManager RTV スロットの確保に使う
    /// @param hwnd 出力先ウィンドウ
    /// @param width  初期幅
    /// @param height 初期高さ
    void Initialize(ID3D12Device* device, IDXGIFactory7* dxgiFactory, ID3D12CommandQueue* commandQueue,
        DescriptorManager* descriptorManager, HWND hwnd, std::int32_t width, std::int32_t height);

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
    HWND hwnd_ = nullptr;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;

    // DescriptorManager の参照（RTVスロット管理・ダングリングポインタ防止）
    DescriptorManager* descriptorManager_ = nullptr;
    ID3D12Device* device_ = nullptr;

    // 初回初期化フラグ
    bool isInitialized_ = false;
};
}

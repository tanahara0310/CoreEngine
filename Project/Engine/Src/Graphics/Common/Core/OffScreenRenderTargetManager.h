#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace CoreEngine
{
// 前方宣言
class DescriptorManager;

/// @brief オフスクリーンレンダリングターゲット管理クラス
class OffScreenRenderTargetManager {
public:
    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param descriptorManager ディスクリプタマネージャー
    /// @param width 初期幅
    /// @param height 初期高さ
    void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, std::int32_t width, std::int32_t height);

    /// @brief オフスクリーンリソースをリサイズ
    void Resize(std::int32_t width, std::int32_t height);

    // オフスクリーン用のアクセッサ（1枚目）
    ID3D12Resource* GetOffScreenResource() const { return offScreenResource_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreenRtvHandle() const { return offscreenRtvHandle_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreenSrvHandle() const { return offscreenSrvHandle_; }

    // オフスクリーン用のアクセッサ（2枚目）
    ID3D12Resource* GetOffScreen2Resource() const { return offScreen2Resource_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreen2RtvHandle() const { return offscreen2RtvHandle_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreen2SrvHandle() const { return offscreen2SrvHandle_; }

private:
    /// @brief オフスクリーン用のレンダリングターゲットを作成
    void CreateOffScreenRenderTarget(std::int32_t width, std::int32_t height);

    /// @brief 既存ディスクリプタにビューを再作成
    void UpdateViews();

private:
    // 1枚目のオフスクリーンバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> offScreenResource_;
    D3D12_CPU_DESCRIPTOR_HANDLE offscreenRtvHandle_ {};
    D3D12_CPU_DESCRIPTOR_HANDLE offscreenSrvCpuHandle_ {};
    D3D12_GPU_DESCRIPTOR_HANDLE offscreenSrvHandle_ {};

    // 2枚目のオフスクリーンバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> offScreen2Resource_;
    D3D12_CPU_DESCRIPTOR_HANDLE offscreen2RtvHandle_ {};
    D3D12_CPU_DESCRIPTOR_HANDLE offscreen2SrvCpuHandle_ {};
    D3D12_GPU_DESCRIPTOR_HANDLE offscreen2SrvHandle_ {};

    // 依存関係
    ID3D12Device* device_ = nullptr;
    DescriptorManager* descriptorManager_ = nullptr;
    bool isInitialized_ = false;
};
}


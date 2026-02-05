#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace CoreEngine
{
// 前方宣言
class DescriptorManager;

/// @brief 深度ステンシル管理クラス（リソース管理のみ）
class DepthStencilManager {
public:
    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param descriptorManager ディスクリプタマネージャー
    /// @param width 幅
    /// @param height 高さ
    void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager,
        std::int32_t width, std::int32_t height);

    /// @brief リソースのみ再作成（DSVは再利用）
    /// @param width 新しい幅
    /// @param height 新しい高さ
    void ResizeResource(std::int32_t width, std::int32_t height);

    // アクセッサ
    ID3D12Resource* GetDepthStencilResource() const { return depthStencilResource_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvHandle_; }

private:
    /// @brief 深度ステンシルリソースの作成
    void CreateDepthStencilResource();

    /// @brief 深度ステンシルビューの作成
    void CreateDepthStencilView();

    /// @brief 深度ステンシルビューの更新（既存のハンドルを使用）
    void UpdateDepthStencilView();

private:
    // 深度ステンシルリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

    // DSVハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};

    // 初期化パラメータ
    ID3D12Device* device_ = nullptr;
    DescriptorManager* descriptorManager_ = nullptr;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;

    // 初回初期化フラグ
    bool isInitialized_ = false;
};
}

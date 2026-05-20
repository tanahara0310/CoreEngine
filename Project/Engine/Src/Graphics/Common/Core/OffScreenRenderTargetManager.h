#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "Graphics/Render/Render.h"

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
    void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, std::int32_t width, std::int32_t height, uint32_t initialTargetCount = 2);

    /// @brief オフスクリーンリソースをリサイズ
    void Resize(std::int32_t width, std::int32_t height);

    /// @brief 指定枚数までオフスクリーンターゲットを確保
    void EnsureTargetCount(uint32_t count);

    /// @brief オフスクリーンターゲット数を取得
    uint32_t GetTargetCount() const { return static_cast<uint32_t>(offScreenTargets_.size()); }

    // オフスクリーン用のアクセッサ（任意インデックス）
    ID3D12Resource* GetOffScreenResource(uint32_t index = 0) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreenRtvHandle(uint32_t index = 0) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreenSrvHandle(uint32_t index = 0) const;

    /// @brief 指定ターゲットのクリアカラーを設定（リソース再作成が必要な場合は Resize/Ensure 後に呼ぶこと）
    void SetTargetClearColor(uint32_t index, const float color[4]);

    /// @brief 指定インデックスのUAVハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreenUavHandle(uint32_t index = 0) const;

    /// @brief 指定ターゲットの現在のリソース状態を取得
    D3D12_RESOURCE_STATES GetOffScreenState(uint32_t index = 0) const;

    /// @brief 指定ターゲットの現在のリソース状態を設定
    void SetOffScreenState(uint32_t index, D3D12_RESOURCE_STATES state);

private:
    struct OffScreenTarget {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE uavHandle{};
        float clearColor[4] = { Render::kClearColor[0], Render::kClearColor[1], Render::kClearColor[2], Render::kClearColor[3] };
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

    /// @brief 単一ターゲットのリソースを作成または再作成
    void CreateOrResizeTargetResource(OffScreenTarget& target, uint32_t index, std::int32_t width, std::int32_t height);

    /// @brief 単一ターゲットのビューを新規作成
    void CreateTargetViews(OffScreenTarget& target, uint32_t index);

    /// @brief 単一ターゲットのビューを更新
    void UpdateTargetViews(const OffScreenTarget& target);

    /// @brief インデックスの妥当性を検証
    void ValidateIndex(uint32_t index) const;

private:
    std::vector<OffScreenTarget> offScreenTargets_;

    // 依存関係
    ID3D12Device* device_ = nullptr;
    DescriptorManager* descriptorManager_ = nullptr;
    bool isInitialized_ = false;
    std::int32_t currentWidth_ = 0;
    std::int32_t currentHeight_ = 0;
};
}


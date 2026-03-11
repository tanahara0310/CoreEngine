#pragma once
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

namespace CoreEngine
{

class DirectXCommon;

/// @brief レンダリング管理クラス
/// レンダーターゲットの管理とフレーム処理を担当
class Render {
public:
    // 統一クリアカラー
    static constexpr float kClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

    /// @brief 初期化
    /// @param dxCommon DirectXCommon
    /// @param dsvHeap DSVヒープ
    void Initialize(DirectXCommon* dxCommon, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap);

    // ===== RenderTargetManager API =====

    /// @brief RenderTargetManagerを取得
    /// @return RenderTargetManager
    RenderTargetManager* GetRenderTargetManager() { return renderTargetManager_.get(); }

    /// @brief RenderTargetManagerを取得（const版）
    /// @return RenderTargetManager
    const RenderTargetManager* GetRenderTargetManager() const { return renderTargetManager_.get(); }

    /// @brief 名前でレンダーターゲットを取得（ショートカット）
    /// @param name ターゲット名
    /// @return レンダーターゲット（見つからない場合はnullptr）
    RenderTarget* GetRenderTarget(const std::string& name);

    // ===== フレーム管理 =====

    /// @brief バックバッファの最終処理（コマンド実行、Present）
    void FinalizeFrame();

    /// @brief DirectXCommonを取得
    /// @return DirectXCommon
    DirectXCommon* GetDirectXCommon() const { return dxCommon_; }

private:
    // クラスをポインタで保持
    DirectXCommon* dxCommon_ = nullptr;

    // DSVヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

    // RenderTargetManager
    std::unique_ptr<RenderTargetManager> renderTargetManager_;
};
}

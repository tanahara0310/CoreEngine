#pragma once
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/BackBufferRenderTarget.h"
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <array>

namespace CoreEngine
{

class DirectXCommon;

class Render {
public:
    // 統一クリアカラー
    static constexpr float kClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

    /// @brief オフスクリーンターゲットの数
    static constexpr int kOffscreenCount = 2;

    /// @brief 初期化
    /// @param dxCommon DirectXCommon
    /// @param dsvHeap DSVヒープ
    void Initialize(DirectXCommon* dxCommon, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap);

    // ===== RenderTarget API =====

    /// @brief オフスクリーンターゲットを取得
    /// @param index オフスクリーンのインデックス（0 or 1）
    /// @return オフスクリーンレンダーターゲット
    RenderTarget* GetOffscreenTarget(int index);

    /// @brief バックバッファターゲットを取得
    /// @return バックバッファレンダーターゲット
    RenderTarget* GetBackBufferTarget();

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

    // RenderTarget
    std::array<std::unique_ptr<OffscreenRenderTarget>, kOffscreenCount> offscreenTargets_;
    std::unique_ptr<BackBufferRenderTarget> backBufferTarget_;
};
}

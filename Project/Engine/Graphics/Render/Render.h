#pragma once
#include "Engine/Graphics/PipelineStateManager.h"
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{

class DirectXCommon;

class Render {
public: // メンバ関数
    // 統一クリアカラー（黒）
    static constexpr float kClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

    /// <summary>                                    
    /// 初期化                                    
    /// </summary>
    /// <param name="dxCommon">DirectXCommon</param>
    /// <param name="dsvHeap">DSVヒープ</param>
    void Initialize(DirectXCommon* dxCommon, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap);

    /// @brief オフスクリーンの描画前処理
    /// @param offscreenIndex オフスクリーンのインデックス（0=1枚目、1=2枚目）
    void OffscreenPreDraw(int offscreenIndex = 0);

    /// @brief 描画前処理(バックバッファ用)
    void BackBufferPreDraw();

    /// @brief オフスクリーンの描画後処理
    /// @param offscreenIndex オフスクリーンのインデックス（0=1枚目、1=2枚目）
    void OffscreenPostDraw(int offscreenIndex = 0);

    /// @brief バックバッファ用の描画後処理
    void BackBufferPostDraw();

    /// @brief オフスクリーンのRTVハンドルを取得
    /// @param offscreenIndex オフスクリーンのインデックス（0=1枚目、1=2枚目）
    /// @return RTVハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE GetOffscreenRTVHandle(int offscreenIndex = 0) const;

    /// @brief DSVハンドルを取得
    /// @return DSVハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const;

    /// @brief ビューポートを取得
    /// @return ビューポート
    D3D12_VIEWPORT GetViewport() const { return viewport_; }

    /// @brief シザー矩形を取得
    /// @return シザー矩形
    D3D12_RECT GetScissorRect() const { return scissorRect_; }

    /// @brief リソースバリアを設定
    /// @param resource 遷移対象のリソース
    /// @param stateBefore 現在のステート
    /// @param stateAfter 遷移先のステート
    void ResourceBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);

private:
    /// @brief オフスクリーンの描画前処理（汎用）
    /// @param resource 対象のオフスクリーンリソース
    /// @param rtvHandle 対象のRTVハンドル
    void OffscreenPreDrawCommon(ID3D12Resource* resource, const D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle);

    /// @brief オフスクリーンの描画後処理（汎用）
    /// @param resource 対象のオフスクリーンリソース
    void OffscreenPostDrawCommon(ID3D12Resource* resource);

    // クラスをポインタで保持
    DirectXCommon* dxCommon_ = nullptr;

    // DSVヒープとサイズ（DirectXCommonから取得）
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    UINT dsvSize_ = 0;

    // オフスクリーンリソース情報（DirectXCommonから取得）
    ID3D12Resource* offscreenResource_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE offscreenRtvHandle_ {};
    ID3D12Resource* offscreen2Resource_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE offscreen2RtvHandle_ {};

    // ビューポートとシザー矩形
    D3D12_VIEWPORT viewport_ {};
    D3D12_RECT scissorRect_ {};
};
}

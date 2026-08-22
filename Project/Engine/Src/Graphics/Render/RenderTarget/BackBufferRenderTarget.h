#pragma once
#include "RenderTarget.h"

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief バックバッファレンダーターゲット
    /// スワップチェーンのバックバッファへの最終描画に使用
    class BackBufferRenderTarget : public RenderTarget {
    public:
        BackBufferRenderTarget() = default;
        ~BackBufferRenderTarget() override = default;

        /// @brief 初期化
        /// @param dx GraphicsCore
        void Initialize(GraphicsCore* dx);

        /// @brief レンダリング開始
        void Begin(ID3D12GraphicsCommandList* cmdList) override;

        /// @brief レンダリング終了
        void End(ID3D12GraphicsCommandList* cmdList) override;

        /// @brief RTVハンドルを取得
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() const override;

        /// @brief SRVハンドルを取得（バックバッファはSRVなし）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const override { return {}; }

        /// @brief リソースを取得
        ID3D12Resource* GetResource() const override;

        /// @brief サイズを取得
        void GetSize(int32_t& width, int32_t& height) const override;

        /// @brief 幅を取得
        /// @details キャッシュせず GraphicsCore から都度取得する（リサイズ通知の配線が不要になる）
        int32_t GetWidth() const override;

        /// @brief 高さを取得
        /// @details キャッシュせず GraphicsCore から都度取得する（リサイズ通知の配線が不要になる）
        int32_t GetHeight() const override;

        /// @brief 現在のバックバッファインデックスを取得
        UINT GetCurrentBackBufferIndex() const;

        /// @brief 現在のバックバッファリソース状態参照を取得
        /// @return 自動バリア処理が更新する状態変数への参照
        D3D12_RESOURCE_STATES& GetCurrentState() { return currentState_; }

        /// @brief 現在のバックバッファリソース状態を取得
        /// @return 追跡中のバックバッファ状態
        D3D12_RESOURCE_STATES GetCurrentState() const { return currentState_; }

    private:
        GraphicsCore* dxCommon_ = nullptr;
        D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_PRESENT;
    };
}

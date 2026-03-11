#pragma once
#include "RenderTarget.h"

namespace CoreEngine
{
    class DirectXCommon;

    /// @brief バックバッファレンダーターゲット
    /// スワップチェーンのバックバッファへの最終描画に使用
    class BackBufferRenderTarget : public RenderTarget {
    public:
        BackBufferRenderTarget() = default;
        ~BackBufferRenderTarget() override = default;

        /// @brief 初期化
        /// @param dx DirectXCommon
        void Initialize(DirectXCommon* dx);

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
        int32_t GetWidth() const override { return width_; }

        /// @brief 高さを取得
        int32_t GetHeight() const override { return height_; }

        /// @brief 現在のバックバッファインデックスを取得
        UINT GetCurrentBackBufferIndex() const;

    private:
        DirectXCommon* dxCommon_ = nullptr;
        int32_t width_ = 0;
        int32_t height_ = 0;
    };
}

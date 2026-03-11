#pragma once
#include "RenderTarget.h"

namespace CoreEngine
{
    class DirectXCommon;

    /// @brief オフスクリーンレンダーターゲット
    /// ポストエフェクトやマルチパスレンダリングで使用
    class OffscreenRenderTarget : public RenderTarget {
    public:
        OffscreenRenderTarget() = default;
        ~OffscreenRenderTarget() override = default;

        /// @brief 初期化
        /// @param dx DirectXCommon
        /// @param index オフスクリーンのインデックス
        void Initialize(DirectXCommon* dx, int index);

        /// @brief レンダリング開始
        void Begin(ID3D12GraphicsCommandList* cmdList) override;

        /// @brief レンダリング終了
        void End(ID3D12GraphicsCommandList* cmdList) override;

        /// @brief RTVハンドルを取得
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() const override { return rtvHandle_; }

        /// @brief SRVハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const override { return srvHandle_; }

        /// @brief リソースを取得
        ID3D12Resource* GetResource() const override { return resource_; }

        /// @brief サイズを取得
        void GetSize(int32_t& width, int32_t& height) const override;

        /// @brief 幅を取得
        int32_t GetWidth() const override { return width_; }

        /// @brief 高さを取得
        int32_t GetHeight() const override { return height_; }

        /// @brief オフスクリーンインデックスを取得
        int GetIndex() const { return index_; }

    private:
        DirectXCommon* dxCommon_ = nullptr;
        ID3D12Resource* resource_ = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
        int32_t width_ = 0;
        int32_t height_ = 0;
        int index_ = 0;
    };
}

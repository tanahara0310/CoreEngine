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
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() const override;

        /// @brief SRVハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const override;

        /// @brief リソースを取得
        ID3D12Resource* GetResource() const override;

        /// @brief サイズを取得
        void GetSize(int32_t& width, int32_t& height) const override;

        /// @brief 幅を取得
        int32_t GetWidth() const override;

        /// @brief 高さを取得
        int32_t GetHeight() const override;

        /// @brief オフスクリーンインデックスを取得
        int GetIndex() const { return index_; }

        /// @brief UAVハンドルを取得（CSポストエフェクト用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetUAVHandle() const;

        /// @brief CSエフェクト用: UAV状態に遷移して書き込み準備をする
        void BeginCS(ID3D12GraphicsCommandList* cmdList);

        /// @brief CSエフェクト用: NON_PIXEL_SHADER_RESOURCE状態に遷移してSRVとして使えるようにする
        void EndCS(ID3D12GraphicsCommandList* cmdList);

        /// @brief 深度バッファを使用するか設定（falseにするとDSVのバインドとクリアをスキップ）
        /// @note SSAO等のポストプロセスパスではfalseにして共有DSVを破壊しないようにする
        void SetUseDepthBuffer(bool use) { useDepthBuffer_ = use; }
        bool GetUseDepthBuffer() const { return useDepthBuffer_; }

        /// @brief 現在のリソース状態を外部から強制設定する
        /// @note SceneView ping-pong 後など、実際の状態が外部で変更された場合に使用
        void SetCurrentState(D3D12_RESOURCE_STATES state) { currentState_ = state; }
        D3D12_RESOURCE_STATES GetCurrentState() const { return currentState_; }

    private:
        void SyncCurrentState() const;

        DirectXCommon* dxCommon_ = nullptr;
        mutable ID3D12Resource* resource_ = nullptr;
        mutable D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
        mutable D3D12_GPU_DESCRIPTOR_HANDLE srvHandle_{};
        mutable D3D12_GPU_DESCRIPTOR_HANDLE uavHandle_{};
        mutable D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
        mutable int32_t width_ = 0;
        mutable int32_t height_ = 0;
        int index_ = 0;
        bool useDepthBuffer_ = true;
        D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };
}

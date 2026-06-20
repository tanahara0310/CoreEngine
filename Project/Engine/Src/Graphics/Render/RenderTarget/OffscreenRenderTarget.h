#pragma once
#include "RenderTarget.h"
#include "Graphics/Common/Core/DescriptorHandle.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"

#include <wrl.h>

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;

    /// @brief オフスクリーンレンダーターゲット
    /// ポストエフェクトやマルチパスレンダリングで使用
    class OffscreenRenderTarget : public RenderTarget {
    public:
        OffscreenRenderTarget() = default;
        ~OffscreenRenderTarget() override;

        /// @brief 初期化
        /// @param dx DirectXCommon
        /// @param descriptorManager ディスクリプタマネージャー
        /// @param desc レンダーターゲット記述子
        /// @param index 内部識別用インデックス
        void Initialize(DirectXCommon* dx, DescriptorManager* descriptorManager, const RenderTargetDescriptor& desc, int index);

        /// @brief リサイズ
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void Resize(uint32_t width, uint32_t height);

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

        /// @brief 外部 DSV を使用するよう設定する
        /// @param dsvHandle 専用深度バッファの DSV ハンドル
        /// @note 未設定時は従来通り DirectXCommon の共有 DSV を使用する
        void SetDepthStencilHandle(D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle) {
            customDsvHandle_ = dsvHandle;
            useCustomDsvHandle_ = true;
        }

        /// @brief 外部 DSV の使用を解除する
        void ResetDepthStencilHandle() { useCustomDsvHandle_ = false; }

        /// @brief 現在のリソース状態を外部から強制設定する
        /// @note 複数 View 実行後など、実際の状態が外部で変更された場合に使用
        void SetCurrentState(D3D12_RESOURCE_STATES state);
        /// @brief 現在のリソース状態参照を取得する
        /// @return 外部の自動バリア処理が更新する状態変数への参照
        D3D12_RESOURCE_STATES& GetCurrentState();
        D3D12_RESOURCE_STATES GetCurrentState() const;

    private:
        void CreateOrResizeResource(uint32_t width, uint32_t height);
        void CreateViews();
        void UpdateViews() const;
        void ReleaseDescriptorHandles();

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
        DescriptorHandle rtvDescriptor_{};
        DescriptorHandle srvDescriptor_{};
        DescriptorHandle uavDescriptor_{};
        mutable D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
        int32_t width_ = 0;
        int32_t height_ = 0;
        DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
        int index_ = 0;
        bool useDepthBuffer_ = true;
        bool autoResize_ = true;
        bool useCustomDsvHandle_ = false;
        D3D12_CPU_DESCRIPTOR_HANDLE customDsvHandle_{};
        D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };
}

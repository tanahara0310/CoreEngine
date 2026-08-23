#pragma once
#include "RenderTarget.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"

#include <wrl.h>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;

    /// @brief オフスクリーンレンダーターゲット
    /// ポストエフェクトやマルチパスレンダリングで使用
    class OffscreenRenderTarget : public RenderTarget {
    public:
        OffscreenRenderTarget() = default;
        ~OffscreenRenderTarget() override;

        /// @brief 初期化
        /// @param dx GraphicsCore
        /// @param descriptorAllocator ディスクリプタマネージャー
        /// @param desc レンダーターゲット記述子
        /// @param index 内部識別用インデックス
        void Initialize(GraphicsCore* dx, DescriptorAllocator* descriptorAllocator, const RenderTargetDescriptor& desc, int index);

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
        /// @note 未設定時は従来通り GraphicsCore の共有 DSV を使用する
        void SetDepthStencilHandle(D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle) {
            customDsvHandle_ = dsvHandle;
            useCustomDsvHandle_ = true;
        }

        /// @brief 外部 DSV の使用を解除する
        void ResetDepthStencilHandle() { useCustomDsvHandle_ = false; }

        /// @brief 直近の Begin() で実際にバインドした DSV ハンドルを返す
        /// @details MRT へ張り替えたいパス（WaterSurfacePass）が、Begin() の
        ///          カスタム DSV 選択ロジックを二重実装せずに済むようにするための入口。
        ///          Begin() を呼ぶ前の値は不定。
        D3D12_CPU_DESCRIPTOR_HANDLE GetBoundDSVHandle() const { return dsvHandle_; }

        /// @brief リソースをステート追跡つきで返す（バリア発行はこれを渡す）
        /// @note 旧 API の `SetCurrentState()`（＝ステートの真実が外にもあることの自白）は廃止した
        GpuResource& Resource() override { return resource_; }

    private:
        void CreateOrResizeResource(uint32_t width, uint32_t height);
        void CreateViews();
        void UpdateViews() const;
        void ReleaseDescriptorHandles();

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        GpuResource resource_;
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
    };
}

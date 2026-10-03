#pragma once
#include "RenderTarget.h"
#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"

#include <wrl.h>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class SceneDepth;

    /// @brief オフスクリーンレンダーターゲット
    /// ポストエフェクトやマルチパスレンダリングで使用
    class OffscreenRenderTarget : public RenderTarget {
    public:
        OffscreenRenderTarget() = default;

        /// @brief 初期化
        /// @param dx GraphicsCore
        /// @param descriptorAllocator ディスクリプタマネージャー
        /// @param sharedDepth 共有するシーン深度（DSV の供給元。深度を使わないターゲットでも渡してよい）
        /// @param desc レンダーターゲット記述子
        /// @param index 内部識別用インデックス
        void Initialize(GraphicsCore* dx, DescriptorAllocator* descriptorAllocator, SceneDepth* sharedDepth,
                        const RenderTargetDescriptor& desc, int index);

        /// @brief リサイズ
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void Resize(uint32_t width, uint32_t height);

        /// @brief レンダリング開始（深度は記述子で深度ありにしたものだけ、desc で外せる）
        RenderTargetBinding Begin(ID3D12GraphicsCommandList* cmdList, const RenderTargetBeginDesc& desc = {}) override;

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

        /// @brief リソースをステート追跡つきで返す（バリア発行はこれを渡す）
        /// @note ステートの更新は GpuResource 側でのみ行う
        GpuResource& Resource() override { return resource_; }

    private:
        void CreateOrResizeResource(uint32_t width, uint32_t height);
        void CreateViews();
        void UpdateViews() const;

        /// @brief 共有シーン深度の DSV（無ければ ptr が 0）
        D3D12_CPU_DESCRIPTOR_HANDLE ResolveDsvHandle() const;

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        SceneDepth* sharedDepth_ = nullptr;
        GpuResource resource_;
        UniqueDescriptor rtvDescriptor_;
        UniqueDescriptor srvDescriptor_;
        UniqueDescriptor uavDescriptor_;
        int32_t width_ = 0;
        int32_t height_ = 0;
        DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
        int index_ = 0;
        bool needsDepthStencil_ = true; ///< 作ったときの記述子で深度ありにしたか
        bool autoResize_ = true;
    };
}

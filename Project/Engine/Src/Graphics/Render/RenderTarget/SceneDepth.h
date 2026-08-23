#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
    class DescriptorAllocator;

    /// @brief メインシーンの深度バッファ（GBuffer / Geometry が書き、各パスが SRV で読む）
    /// @details リソース・DSV・深度 SRV・現在ステートを 1 箇所で持つ。
    ///          旧 DepthStencilManager は RHI 層（GraphicsCore の所有）にあったが、
    ///          「D24S8 固定・BeginDepthWrite＝バリア＋クリアの描画ポリシー」はレンダラの都合なので
    ///          Render 層へ移した。所有者は RenderDomainContext（GBuffer と同じ寿命・同じリサイズ順）。
    ///
    ///          DSV / SRV のスロットはリサイズしても変わらない（同じスロットへ書き直す）。
    ///          したがって初期化時に取ったハンドル値はずっと有効で、
    ///          OffscreenRenderTarget はそれを共有 DSV として使う。
    class SceneDepth {
    public:
        /// @brief デストラクタ（確保したディスクリプタスロットを解放）
        ~SceneDepth();

        /// @brief 初期化
        /// @param device D3D12デバイス
        /// @param descriptorAllocator ディスクリプタの確保先
        /// @param width 幅
        /// @param height 高さ
        void Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator,
            std::int32_t width, std::int32_t height);

        /// @brief リソースのみ再作成（DSV / SRV は同じスロットへ書き直す）
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void ResizeResource(std::int32_t width, std::int32_t height);

        // ---------------------------------------------------------------
        // バリア遷移 / クリア操作
        // ---------------------------------------------------------------

        /// @brief DEPTH_WRITE 状態へ遷移し、深度バッファをクリアする
        /// @note フレーム先頭や GBuffer パス開始前に呼ぶ
        void BeginDepthWrite(ID3D12GraphicsCommandList* cmdList);

        // ---------------------------------------------------------------
        // アクセッサ
        // ---------------------------------------------------------------
        ID3D12Resource* GetResource() const { return depthStencilResource_.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvDescriptor_.cpuHandle; }
        /// @brief 深度テクスチャの SRV GPU ハンドルを返す（シェーダーからサンプリングするために使用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSRVHandle() const { return depthSRVDescriptor_.gpuHandle; }
        /// @brief 深度リソースをステート追跡つきで返す（バリア発行はこれを渡す）
        GpuResource& Resource() { return depthStencilResource_; }

        std::int32_t GetWidth() const { return width_; }
        std::int32_t GetHeight() const { return height_; }

    private:
        /// @brief 深度ステンシルリソースの作成
        void CreateDepthStencilResource();

        /// @brief 深度ステンシルビューの作成
        void CreateDepthStencilView();

        /// @brief 深度ステンシルビューの更新（既存のハンドルを使用）
        void UpdateDepthStencilView();

        /// @brief 深度リソースの SRV を作成する（シェーダーからのサンプリング用）
        void CreateDepthShaderResourceView();

    private:
        // 深度ステンシルリソース（現在ステートは GpuResource が内包する）
        GpuResource depthStencilResource_;

        // DSV / SRV スロット。DescriptorHandle が「どのスロットを所有しているか」まで表すので、
        // CPU/GPU ハンドルとインデックスを別々に持つ必要がない（旧実装は 5 変数に分かれていた）。
        DescriptorHandle dsvDescriptor_{};
        // 深度 SRV（Water Depth Fade 等でシェーダーからサンプリングするために使用）
        DescriptorHandle depthSRVDescriptor_{};

        // 初期化パラメータ
        ID3D12Device* device_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        std::int32_t width_ = 0;
        std::int32_t height_ = 0;

        // 初回初期化フラグ
        bool isInitialized_ = false;
    };
}

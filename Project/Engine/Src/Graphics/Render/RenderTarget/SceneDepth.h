#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"
#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
    class DescriptorAllocator;
    class UploadRing;

    /// @brief メインシーンの深度バッファ（GBuffer / Geometry が書き、各パスが SRV で読む）
    /// @details リソース・DSV・深度 SRV・現在ステートを 1 箇所で持つ。
    ///          シェーダーグラフの Scene Depth が半透明で読む深度の写しと、深度を直す値の定数も持つ。
    /// @note DSV / SRV のスロットはリサイズしても変わらないので、取得済みハンドルは常に有効
    class SceneDepth {
    public:
        /// @brief シェーダが深度を直す値（SceneDepth.hlsli の SceneDepthParams と一致させる）
        struct ShaderParams {
            float nearZ = 0.1f;
            float farZ = 1000.0f;
            float screenWidth = 1.0f;
            float screenHeight = 1.0f;
            float orthographic = 0.0f; ///< 1 = 平行投影
            float padding[3] = {};
        };

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
        D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvDescriptor_.Cpu(); }
        /// @brief 深度テクスチャの SRV GPU ハンドルを返す（シェーダーからサンプリングするために使用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSRVHandle() const { return depthSRVDescriptor_.Gpu(); }
        /// @brief 深度リソースをステート追跡つきで返す（バリア発行はこれを渡す）
        GpuResource& Resource() { return depthStencilResource_; }

        std::int32_t GetWidth() const { return width_; }
        std::int32_t GetHeight() const { return height_; }

        // ---------------------------------------------------------------
        // シェーダが読む深度（シェーダーグラフの Scene Depth）
        // ---------------------------------------------------------------

        /// @brief 「奥に何も無い」ときに差す 1×1 のテクスチャ（値が 1）を控える
        void SetFarTexture(D3D12_GPU_DESCRIPTOR_HANDLE handle) { farTextureSRV_ = handle; }
        /// @brief 「奥に何も無い」1×1 のテクスチャ
        D3D12_GPU_DESCRIPTOR_HANDLE GetFarTextureSRV() const { return farTextureSRV_; }

        /// @brief フレームの初めに深度を直す値を詰め、このフレームで深度を写すかを決める
        /// @details 前のフレームで半透明が深度を読んでいれば写す（写しの実体はここで用意する）
        void BeginShaderRead(UploadRing& uploadRing, const ShaderParams& params);

        /// @brief このフレームで深度を写すか
        bool IsCopyWanted() const { return copyWanted_; }

        /// @brief 写しをステート追跡つきで返す（用意していなければ空）
        GpuResource& ShaderCopy() { return copyResource_; }
        /// @brief 写しの SRV（用意していなければ 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetShaderCopySRVHandle() const;

        /// @brief 深度を写しへ写す（この後の半透明は写しを読む）
        void CopyForShaderRead(ID3D12GraphicsCommandList* cmdList);

        /// @brief 半透明のシェーダに差す深度（このフレームに写していれば写し・まだなら「奥に何も無い」テクスチャ）
        /// @details 呼ぶと次のフレームでも写す
        D3D12_GPU_DESCRIPTOR_HANDLE AcquireTransparentReadSRV();

        /// @brief 深度を直す値の定数（`gSceneDepthParams`。BeginShaderRead の前は 0）
        D3D12_GPU_VIRTUAL_ADDRESS GetShaderParams() const { return shaderParams_; }

    private:
        /// @brief 写しの実体と SRV を用意する（大きさが変わったら作り直す）
        bool EnsureShaderCopy();

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

        // DSV / SRV スロット（破棄すると GPU が使い終わってから返る）
        UniqueDescriptor dsvDescriptor_;
        // 深度 SRV（Water Depth Fade 等でシェーダーからサンプリングするために使用）
        UniqueDescriptor depthSRVDescriptor_;

        // 初期化パラメータ
        ID3D12Device* device_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        std::int32_t width_ = 0;
        std::int32_t height_ = 0;

        // 初回初期化フラグ
        bool isInitialized_ = false;

        // シェーダが読む深度の写し（初めて頼まれたときに作る）
        GpuResource copyResource_;
        UniqueDescriptor copySRVDescriptor_;
        std::int32_t copyWidth_ = 0;
        std::int32_t copyHeight_ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE farTextureSRV_ = {};
        D3D12_GPU_VIRTUAL_ADDRESS shaderParams_ = 0;
        bool copyRequested_ = false; ///< このフレームで半透明が深度を読んだ
        bool copyWanted_ = false;    ///< このフレームで写す
        bool copyReady_ = false;     ///< このフレームで写し終えた
    };
}

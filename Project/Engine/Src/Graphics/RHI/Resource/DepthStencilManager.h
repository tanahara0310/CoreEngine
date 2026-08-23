#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
// 前方宣言
class DescriptorAllocator;

/// @brief 深度ステンシル管理クラス
/// リソース管理・バリア遷移・クリア・SRVアクセスをすべて一か所で担う
class DepthStencilManager {
public:
    /// @brief デストラクタ（確保したディスクリプタスロットを解放）
    ~DepthStencilManager();

    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param descriptorAllocator ディスクリプタマネージャー
    /// @param width 幅
    /// @param height 高さ
    void Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator,
        std::int32_t width, std::int32_t height);

    /// @brief リソースのみ再作成（DSVは再利用）
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
    ID3D12Resource* GetDepthStencilResource() const { return depthStencilResource_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvDescriptor_.cpuHandle; }
    /// @brief 深度テクスチャの SRV GPU ハンドルを返す（シェーダーからサンプリングするために使用）
    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSRVHandle() const { return depthSRVDescriptor_.gpuHandle; }
    /// @brief 深度リソースをステート追跡つきで返す（バリア発行はこれを渡す）
    GpuResource& Resource() { return depthStencilResource_; }

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

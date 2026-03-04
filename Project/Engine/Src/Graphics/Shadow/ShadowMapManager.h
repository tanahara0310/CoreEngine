#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    // 前方宣言
    class DescriptorManager;

    /// @brief シャドウマップ管理クラス
    /// @note ライトVP行列とシャドウマップリソースを一元管理
    class ShadowMapManager {
    public:
        /// @brief デフォルトのシャドウマップサイズ
        static constexpr std::uint32_t DEFAULT_SHADOW_MAP_SIZE = 4096;

        /// @brief 初期化
        /// @param device D3D12デバイス
        /// @param descriptorManager ディスクリプタマネージャー
        /// @param shadowMapSize シャドウマップの解像度（デフォルト: 4096）
        void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, 
            std::uint32_t shadowMapSize = DEFAULT_SHADOW_MAP_SIZE);

        /// @brief シャドウマップの深度をクリア
        /// @param cmdList コマンドリスト
        void ClearDepth(ID3D12GraphicsCommandList* cmdList);

        /// @brief リソースバリア: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
        /// @param cmdList コマンドリスト
        void TransitionToDepthWrite(ID3D12GraphicsCommandList* cmdList);

        /// @brief リソースバリア: DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
        /// @param cmdList コマンドリスト
        void TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList);

        // アクセッサ
        ID3D12Resource* GetShadowMapResource() const { return shadowMapResource_.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvHandle_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const { return srvGpuHandle_; }
        std::uint32_t GetShadowMapSize() const { return shadowMapSize_; }

        /// @brief ライトビュープロジェクション行列を設定
        /// @param lightViewProjection ライトから見たビュープロジェクション行列
        void SetLightViewProjection(const Matrix4x4& lightViewProjection);

        /// @brief ライトビュープロジェクション行列を取得
        /// @return ライトビュープロジェクション行列
        const Matrix4x4& GetLightViewProjection() const { return lightViewProjection_; }

    private:
        /// @brief シャドウマップリソースの作成
        void CreateShadowMapResource();

        /// @brief 深度ステンシルビューの作成
        void CreateDepthStencilView();

        /// @brief シェーダーリソースビューの作成
        void CreateShaderResourceView();

    private:
        // シャドウマップリソース
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowMapResource_;

        // DSVハンドル（深度書き込み用）
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};

        // SRVハンドル（シェーダーで読み取り用）
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};

        // シャドウマップのサイズ（動的に設定可能）
        std::uint32_t shadowMapSize_ = DEFAULT_SHADOW_MAP_SIZE;

        // 深度フォーマット
        static constexpr DXGI_FORMAT shadowMapFormat_ = DXGI_FORMAT_D32_FLOAT;

        // 依存関係
        ID3D12Device* device_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;

        // 現在のリソースステート
        D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        // ライトビュープロジェクション行列（一元管理）
        Matrix4x4 lightViewProjection_{};
    };
}

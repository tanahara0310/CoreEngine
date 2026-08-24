#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include <wrl.h>
#include <vector>

#include "LightData.h"
#include "Graphics/RootSignature/RootSlot.h"

namespace CoreEngine
{
    class ShaderBinder;

    class ResourceFactory;
    class DescriptorAllocator;

    /// @brief ライトバッファの管理クラス
    /// GPU用のStructuredBufferの作成、更新、コマンドリストへの設定を担当
    class LightBufferManager
    {
    public:
        /// @brief 初期化（max* は種別ごとのライト最大数）
        void Initialize(
            ID3D12Device* device,
            DescriptorAllocator* descriptorAllocator,
            ResourceFactory* resourceFactory,
            uint32_t maxDirectionalLights,
            uint32_t maxPointLights,
            uint32_t maxSpotLights,
            uint32_t maxAreaLights
        );

        /// @brief ライトバッファを更新
        void UpdateBuffers(
            const std::vector<DirectionalLightData>& directionalLights,
            const std::vector<PointLightData>& pointLights,
            const std::vector<SpotLightData>& spotLights,
            const std::vector<AreaLightData>& areaLights
        );

        /// @brief コマンドリストにライトをセット（ShaderBinder 経由）
        /// @details Set* の選択は RootSlot の種別から ShaderBinder が行う。
        ///          binder が差したことを記録するので、Draw 前の取りこぼし検出が効く。
        void SetToCommandList(
            ShaderBinder& binder,
            RootSlot lightCounts,
            RootSlot directionalLights,
            RootSlot pointLights,
            RootSlot spotLights,
            RootSlot areaLights
        );

        /// @brief コマンドリストにライトをセット（ルートパラメータ番号版）
        /// @deprecated ShaderBinder 版へ移行すること。番号だけでは差し方を検証できない。
        void SetToCommandList(
            ID3D12GraphicsCommandList* commandList,
            int lightCountsRootParameterIndex,
            int directionalLightsRootParameterIndex,
            int pointLightsRootParameterIndex,
            int spotLightsRootParameterIndex,
            int areaLightsRootParameterIndex
        );

        /// @brief ライトカウントバッファのGPU仮想アドレスを取得
        D3D12_GPU_VIRTUAL_ADDRESS GetLightCountsGPUAddress() const;

        /// @brief ディレクショナルライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalLightsSRVHandle() const { return directionalLightsSRVHandle_.gpuHandle; }

        /// @brief ポイントライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetPointLightsSRVHandle() const { return pointLightsSRVHandle_.gpuHandle; }

        /// @brief スポットライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetSpotLightsSRVHandle() const { return spotLightsSRVHandle_.gpuHandle; }

        /// @brief エリアライトSRVのGPUハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetAreaLightsSRVHandle() const { return areaLightsSRVHandle_.gpuHandle; }

    private:
        /// @brief StructuredBuffer用のリソースを作成
        void CreateBufferResources(
            ID3D12Device* device,
            uint32_t maxDirectionalLights,
            uint32_t maxPointLights,
            uint32_t maxSpotLights,
            uint32_t maxAreaLights
        );

        /// @brief StructuredBuffer用のSRVを作成
        void CreateBufferSRVs(
            DescriptorAllocator* descriptorAllocator,
            uint32_t maxDirectionalLights,
            uint32_t maxPointLights,
            uint32_t maxSpotLights,
            uint32_t maxAreaLights
        );

    private:
        // GPU側のStructuredBufferリソース
        Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightsBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> pointLightsBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> spotLightsBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> areaLightsBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCountsBuffer_;

        // StructuredBufferのSRV用GPUハンドル
        DescriptorHandle directionalLightsSRVHandle_{};
        DescriptorHandle pointLightsSRVHandle_{};
        DescriptorHandle spotLightsSRVHandle_{};
        DescriptorHandle areaLightsSRVHandle_{};

        // マップされたライトカウントデータ
        LightCounts* lightCountsData_ = nullptr;
    };
}

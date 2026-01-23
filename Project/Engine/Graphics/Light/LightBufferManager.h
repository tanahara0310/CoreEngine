#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>

#include "LightData.h"

namespace CoreEngine
{
	class ResourceFactory;
	class DescriptorManager;

	/// @brief ライトバッファの管理クラス
	/// GPU用のStructuredBufferの作成、更新、コマンドリストへの設定を担当
	class LightBufferManager
	{
	public:
		/// @brief 初期化
		/// @param device D3D12デバイス
		/// @param resourceFactory リソースファクトリ
		/// @param descriptorManager ディスクリプタマネージャー
		/// @param maxDirectionalLights ディレクショナルライトの最大数
		/// @param maxPointLights ポイントライトの最大数
		/// @param maxSpotLights スポットライトの最大数
		/// @param maxAreaLights エリアライトの最大数
		void Initialize(
			ID3D12Device* device,
			DescriptorManager* descriptorManager,
			uint32_t maxDirectionalLights,
			uint32_t maxPointLights,
			uint32_t maxSpotLights,
			uint32_t maxAreaLights
		);

		/// @brief ライトバッファを更新
		/// @param directionalLights ディレクショナルライトデータ
		/// @param pointLights ポイントライトデータ
		/// @param spotLights スポットライトデータ
		/// @param areaLights エリアライトデータ
		void UpdateBuffers(
			const std::vector<DirectionalLightData>& directionalLights,
			const std::vector<PointLightData>& pointLights,
			const std::vector<SpotLightData>& spotLights,
			const std::vector<AreaLightData>& areaLights
		);

		/// @brief コマンドリストにライトをセット
		/// @param commandList コマンドリスト
		/// @param lightCountsRootParameterIndex ライトカウント用のルートパラメータインデックス
		/// @param directionalLightsRootParameterIndex ディレクショナルライト用のルートパラメータインデックス
		/// @param pointLightsRootParameterIndex ポイントライト用のルートパラメータインデックス
		/// @param spotLightsRootParameterIndex スポットライト用のルートパラメータインデックス
		/// @param areaLightsRootParameterIndex エリアライト用のルートパラメータインデックス
		void SetToCommandList(
			ID3D12GraphicsCommandList* commandList,
			UINT lightCountsRootParameterIndex,
			UINT directionalLightsRootParameterIndex,
			UINT pointLightsRootParameterIndex,
			UINT spotLightsRootParameterIndex,
			UINT areaLightsRootParameterIndex
		);

		/// @brief ライトカウントバッファのGPU仮想アドレスを取得
		D3D12_GPU_VIRTUAL_ADDRESS GetLightCountsGPUAddress() const;

		/// @brief ディレクショナルライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalLightsSRVHandle() const { return directionalLightsSRVHandle_; }

		/// @brief ポイントライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetPointLightsSRVHandle() const { return pointLightsSRVHandle_; }

		/// @brief スポットライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetSpotLightsSRVHandle() const { return spotLightsSRVHandle_; }

		/// @brief エリアライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetAreaLightsSRVHandle() const { return areaLightsSRVHandle_; }

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
			DescriptorManager* descriptorManager,
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
		D3D12_GPU_DESCRIPTOR_HANDLE directionalLightsSRVHandle_{};
		D3D12_GPU_DESCRIPTOR_HANDLE pointLightsSRVHandle_{};
		D3D12_GPU_DESCRIPTOR_HANDLE spotLightsSRVHandle_{};
		D3D12_GPU_DESCRIPTOR_HANDLE areaLightsSRVHandle_{};

		// マップされたライトカウントデータ
		LightCounts* lightCountsData_ = nullptr;
	};
}

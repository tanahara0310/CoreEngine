#pragma once

#include <memory>
#include <vector>
#include <d3d12.h>

#include "LightData.h"
#include "LightBufferManager.h"
#include "LightDebugVisualizer.h"

namespace CoreEngine
{
	class ResourceFactory;
	class DescriptorManager;

	/// @brief ライトマネージャー（ライトの管理と制御を担当）
	class LightManager {
	public:
		static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
		static constexpr uint32_t MAX_POINT_LIGHTS = 16;
		static constexpr uint32_t MAX_SPOT_LIGHTS = 16;
		static constexpr uint32_t MAX_AREA_LIGHTS = 8;

	public:
		/// @brief 初期化
		/// @param device D3D12デバイス
		/// @param resourceFactory リソースファクトリ
		/// @param descriptorManager ディスクリプタマネージャー
		void Initialize(ID3D12Device* device, ResourceFactory* resourceFactory, DescriptorManager* descriptorManager);

		/// @brief 全てのライトを更新
		void UpdateAll();

		/// @brief ライトのImGuiを描画
		void DrawAllImGui();

		/// @brief ディレクショナルライトを追加
		/// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
		DirectionalLightData* AddDirectionalLight();

		/// @brief ポイントライトを追加
		/// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
		PointLightData* AddPointLight();

		/// @brief スポットライトを追加
		/// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
		SpotLightData* AddSpotLight();

		/// @brief エリアライトを追加
		/// @return 追加されたライトデータへのポインタ（最大数を超えた場合はnullptr）
		AreaLightData* AddAreaLight();

		/// @brief コマンドリストにライトをセット
		/// @param commandList コマンドリスト
		/// @param lightCountsRootParameterIndex ライトカウント用のルートパラメータインデックス
		/// @param directionalLightsRootParameterIndex ディレクショナルライト用のルートパラメータインデックス
		/// @param pointLightsRootParameterIndex ポイントライト用のルートパラメータインデックス
		/// @param spotLightsRootParameterIndex スポットライト用のルートパラメータインデックス
		/// @param areaLightsRootParameterIndex エリアライト用のルートパラメータインデックス
		void SetLightsToCommandList(
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
		D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalLightsSRVHandle() const;

		/// @brief ポイントライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetPointLightsSRVHandle() const;

		/// @brief スポットライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetSpotLightsSRVHandle() const;

		/// @brief エリアライトSRVのGPUハンドルを取得
		D3D12_GPU_DESCRIPTOR_HANDLE GetAreaLightsSRVHandle() const;

		/// @brief ディレクショナルライトの有効/無効を設定
		/// @param index ライトのインデックス
		/// @param enabled 有効にする場合true
		void SetDirectionalLightEnabled(size_t index, bool enabled);

		/// @brief ポイントライトの有効/無効を設定
		/// @param index ライトのインデックス
		/// @param enabled 有効にする場合true
		void SetPointLightEnabled(size_t index, bool enabled);

		/// @brief スポットライトの有効/無効を設定
		/// @param index ライトのインデックス
		/// @param enabled 有効にする場合true
		void SetSpotLightEnabled(size_t index, bool enabled);

		/// @brief エリアライトの有効/無効を設定
		/// @param index ライトのインデックス
		/// @param enabled 有効にする場合true
		void SetAreaLightEnabled(size_t index, bool enabled);

		/// @brief ディレクショナルライトを取得
		/// @param index ライトのインデックス
		/// @return ライトデータへのポインタ（範囲外の場合はnullptr）
		DirectionalLightData* GetDirectionalLight(size_t index);

		/// @brief 全てのライトをクリア（シーン切り替え時に使用）
		void ClearAllLights();

		/// @brief メインディレクショナルライトのビュープロジェクション行列を計算
		/// @param sceneCenter シーンの中心座標
		/// @param sceneRadius シーンを囲む半径
		/// @return ライトビュープロジェクション行列
		Matrix4x4 CalculateMainDirectionalLightViewProjection(const Vector3& sceneCenter, float sceneRadius);

	private:
		std::vector<DirectionalLightData> directionalLights_;
		std::vector<PointLightData> pointLights_;
		std::vector<SpotLightData> spotLights_;
		std::vector<AreaLightData> areaLights_;

		LightBufferManager bufferManager_;
		LightDebugVisualizer debugVisualizer_;
	};
}

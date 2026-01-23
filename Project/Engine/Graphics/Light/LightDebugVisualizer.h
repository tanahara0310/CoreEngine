#pragma once

#include <vector>
#include <functional>
#include "LightData.h"

namespace CoreEngine
{
	/// @brief ライトのデバッグ可視化とImGui UI管理クラス
	class LightDebugVisualizer
	{
	public:
		/// @brief ImGui UIを描画
		/// @param directionalLights ディレクショナルライトデータ
		/// @param pointLights ポイントライトデータ
		/// @param spotLights スポットライトデータ
		/// @param areaLights エリアライトデータ
		/// @param maxDirectionalLights ディレクショナルライトの最大数
		/// @param maxPointLights ポイントライトの最大数
		/// @param maxSpotLights スポットライトの最大数
		/// @param maxAreaLights エリアライトの最大数
		/// @param onAddDirectionalLight ディレクショナルライト追加時のコールバック
		/// @param onAddPointLight ポイントライト追加時のコールバック
		/// @param onAddSpotLight スポットライト追加時のコールバック
		/// @param onAddAreaLight エリアライト追加時のコールバック
		void DrawImGui(
			std::vector<DirectionalLightData>& directionalLights,
			std::vector<PointLightData>& pointLights,
			std::vector<SpotLightData>& spotLights,
			std::vector<AreaLightData>& areaLights,
			uint32_t maxDirectionalLights,
			uint32_t maxPointLights,
			uint32_t maxSpotLights,
			uint32_t maxAreaLights,
			const std::function<void()>& onAddDirectionalLight,
			const std::function<void()>& onAddPointLight,
			const std::function<void()>& onAddSpotLight,
			const std::function<void()>& onAddAreaLight
		);

		/// @brief デバッグ可視化を描画
		/// @param directionalLights ディレクショナルライトデータ
		/// @param pointLights ポイントライトデータ
		/// @param spotLights スポットライトデータ
		void DrawVisualization(
			const std::vector<DirectionalLightData>& directionalLights,
			const std::vector<PointLightData>& pointLights,
			const std::vector<SpotLightData>& spotLights
		);

		/// @brief エリアライトのデバッグ可視化を描画
		/// @param light エリアライトデータ
		void DrawAreaLightVisualization(const AreaLightData& light);

		/// @brief デバッグ可視化の有効/無効を設定
		void SetVisualizationEnabled(bool enabled) { enableVisualization_ = enabled; }

		/// @brief デバッグ可視化が有効かどうかを取得
		bool IsVisualizationEnabled() const { return enableVisualization_; }

	private:
		void DrawDirectionalLightVisualization(const DirectionalLightData& light);
		void DrawPointLightVisualization(const PointLightData& light);
		void DrawSpotLightVisualization(const SpotLightData& light);

	private:
#ifdef _DEBUG
		bool enableVisualization_ = true;
#else
		bool enableVisualization_ = false;
#endif
	};
}

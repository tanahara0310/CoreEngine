#pragma once

#include "Graphics/Water/WaterSurfaceData.h"

#include <cstdint>

namespace CoreEngine
{
	/// @brief Water simulation の種類
	enum class WaterSurfaceSimulationType : uint32_t {
		Gerstner = 0,
		FFTOcean = 1,
	};

	/// @brief Water simulation が surface 情報を構築する際の入力値
	struct WaterSurfaceSimulationInput {
		float waterHeight = 0.0f;
		const WaterConstants* gerstnerConstants = nullptr;
	};

	/// @brief Water surface simulation の共通契約
	/// @details Gerstner / FFT の差分を runtime controller から隠蔽する。
	class WaterSurfaceSimulator {
	public:
		virtual ~WaterSurfaceSimulator() = default;

		/// @brief この simulator が担当する simulation 種類を返す
		virtual WaterSurfaceSimulationType GetSimulationType() const = 0;

		/// @brief 現在の simulation 時間を返す
		virtual float GetElapsedTime() const = 0;

		/// @brief simulation の時間進行を更新する
		/// @param deltaTime 前フレームからの経過時間（秒）
		virtual void AdvanceSimulation(float deltaTime) = 0;

		/// @brief 共通 snapshot と DXR 用 surface data を構築する
		/// @param input 現在の水面状態入力
		/// @param snapshot 共通 snapshot の出力先
		/// @param surfaceData DXR 用 surface data の出力先
		virtual void CaptureSurface(
			const WaterSurfaceSimulationInput& input,
			WaterSurfaceSnapshot& snapshot,
			WaterSurfaceData& surfaceData) const = 0;
	};
}

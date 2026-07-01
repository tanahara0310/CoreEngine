#pragma once

#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
	/// @brief 水面モデル由来の DXR 用 surface data を供給する抽象インターフェース
	class IWaterSurfaceModelProvider {
	public:
		virtual ~IWaterSurfaceModelProvider() = default;
		virtual bool TryGetSurfaceData(WaterSurfaceData& outSurfaceData) const = 0;
		virtual WaterSurfaceSimulationType GetSimulationType() const = 0;
		virtual const char* GetProviderName() const = 0;
	};

	/// @brief 外部保持の WaterSurfaceData を供給する軽量プロバイダー
	class StaticWaterSurfaceModelProvider final : public IWaterSurfaceModelProvider {
	public:
		StaticWaterSurfaceModelProvider(
			const WaterSurfaceData* surfaceData,
			WaterSurfaceSimulationType simulationType,
			const char* providerName);

		void SetSource(
			const WaterSurfaceData* surfaceData,
			WaterSurfaceSimulationType simulationType,
			const char* providerName);

		bool TryGetSurfaceData(WaterSurfaceData& outSurfaceData) const override;
		WaterSurfaceSimulationType GetSimulationType() const override;
		const char* GetProviderName() const override;

	private:
		const WaterSurfaceData* surfaceData_ = nullptr;
		WaterSurfaceSimulationType simulationType_ = WaterSurfaceSimulationType::Gerstner;
		const char* providerName_ = "StaticWaterSurfaceModelProvider";
	};
}

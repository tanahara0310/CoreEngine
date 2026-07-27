#pragma once

#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"

namespace CoreEngine
{
    /// @brief FFT Ocean ベースの Water simulation 実装
    /// @details FFT GPU シミュレーション本体は FFTOceanManager を利用し、ここでは CPU 側の simulation 時間を管理する。
    class FFTOceanSurfaceSimulator final : public WaterSurfaceSimulator {
    public:
        WaterSurfaceSimulationType GetSimulationType() const override {
            return WaterSurfaceSimulationType::FFTOcean;
        }

        float GetElapsedTime() const override { return elapsedTime_; }

        void AdvanceSimulation(float deltaTime) override;

        void CaptureSurface(
            const WaterSurfaceSimulationInput& input,
            WaterSurfaceData& surfaceData) const override;
    private:
        float elapsedTime_ = 0.0f;
    };
}

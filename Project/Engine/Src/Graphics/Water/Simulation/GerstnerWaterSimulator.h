#pragma once

#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"

namespace CoreEngine
{
    /// @brief Gerstner Wave ベースの Water simulation 実装
    /// @details WaterConstants を時間更新し、DXR 用 surface data を構築する。
    class GerstnerWaterSimulator final : public WaterSurfaceSimulator {
    public:
        WaterSurfaceSimulationType GetSimulationType() const override {
            return WaterSurfaceSimulationType::Gerstner;
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

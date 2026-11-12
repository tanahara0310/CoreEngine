#pragma once

#include "Graphics/Water/Wave/WaterWaveModel.h"

namespace CoreEngine
{
    /// @brief Gerstner の波（水面コンポーネントが持つ波の和）
    class GerstnerWaveModel final : public WaterWaveModel {
    public:
        WaterWaveModelType GetType() const override { return WaterWaveModelType::Gerstner; }

        WaterWaveCapabilities GetCapabilities() const override { return {}; }

        /// @brief 描いている間だけ時刻を進める
        void Advance(float deltaSeconds, bool drawn) override;

        void Capture(
            RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const override;

        /// @brief 振幅の二乗和から求める
        float ComputeSignificantWaveHeight(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const override;

        /// @brief 振幅の和
        float ComputeMaxCrestHeight(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const override;
    };
}

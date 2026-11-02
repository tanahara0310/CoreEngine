#pragma once

namespace CoreEngine
{
    class LightManager;
    class WaterSurfaceComponent;

    /// @brief 大気の月と太陽の高さから潮位を求める
    class WaterTideModule {
    public:
        /// @brief 月と太陽の高さから潮位を求め、水面へ渡す（真上か真下で満潮、地平線で干潮）
        /// @param lightManager 月と太陽の向きを持つライト（無ければ潮位 0）
        void Update(const LightManager* lightManager, WaterSurfaceComponent& waterPlane) const;
    };
}

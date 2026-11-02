#include "pch.h"
#include "WaterTideModule.h"

#include <cmath>

#include "Graphics/Light/LightManager.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"

namespace CoreEngine
{
    namespace {
        /// @brief 潮を起こす力の重み（月を 1 としたときの太陽）
        constexpr float kMoonTideWeight = 1.0f;
        constexpr float kSunTideWeight = 0.46f;

        /// @brief 月と太陽の高さから潮位を求める
        /// @param amplitude 月と太陽が真上か真下に来たときにトランスフォームの高さより上がる高さ [m]
        /// @return 潮位 [m]。月が無ければ太陽だけで求め、どちらも無ければ 0
        float ComputeSkyTideHeight(float amplitude, const LightManager* lightManager)
        {
            if (amplitude <= 0.0f || !lightManager) {
                return 0.0f;
            }

            float weighted = 0.0f;
            float weightSum = 0.0f;
            const auto accumulate = [&](const Light* light, float weight) {
                if (!light) {
                    return;
                }
                const Vector3& d = light->direction;
                const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (length < 1.0e-6f) {
                    return;
                }
                // 天頂角 θ の cos 2θ（= 2cos²θ − 1）。光の向きの逆が天体の方向
                const float cosZenith = -d.y / length;
                weighted += weight * (2.0f * cosZenith * cosZenith - 1.0f);
                weightSum += weight;
            };

            const Light* const moon = lightManager->GetAtmosphereMoonLight();
            const Light* const sun = lightManager->GetAtmosphereSunLight();
            accumulate(moon, kMoonTideWeight);
            if (sun != moon) {
                accumulate(sun, kSunTideWeight);
            }
            return (weightSum > 0.0f) ? amplitude * weighted / weightSum : 0.0f;
        }
    }

    void WaterTideModule::Update(const LightManager* lightManager, WaterSurfaceComponent& waterPlane) const
    {
        waterPlane.SetSkyTideHeight(ComputeSkyTideHeight(waterPlane.GetTideAmplitude(), lightManager));
    }
}

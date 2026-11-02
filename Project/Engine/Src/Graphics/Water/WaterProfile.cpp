#include "pch.h"
#include "WaterProfile.h"

namespace CoreEngine
{
    namespace
    {
        // 濁り 1 のときにベース値へ足す量 [1/m]。
        // 吸収は CDOM・植物プランクトンによる短波長（青）寄りの吸収、
        // 散乱は懸濁粒子によるほぼ波長によらない散乱
        constexpr float kTurbidityAbsorptionGain[3] = { 0.05f, 0.08f, 0.35f };
        constexpr float kTurbidityScatteringGain[3] = { 0.22f, 0.26f, 0.20f };
    }

    Vector3 WaterProfile::EffectiveAbsorption() const
    {
        return Vector3{
            absorption.x + turbidity * kTurbidityAbsorptionGain[0],
            absorption.y + turbidity * kTurbidityAbsorptionGain[1],
            absorption.z + turbidity * kTurbidityAbsorptionGain[2] };
    }

    Vector3 WaterProfile::EffectiveScattering() const
    {
        return Vector3{
            scattering.x + turbidity * kTurbidityScatteringGain[0],
            scattering.y + turbidity * kTurbidityScatteringGain[1],
            scattering.z + turbidity * kTurbidityScatteringGain[2] };
    }
}

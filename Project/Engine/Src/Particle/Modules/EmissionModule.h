#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
/// @brief 放出データ（EmissionModule）
struct ParticleEmissionData {
    uint32_t rateOverTime = 10;     // 時間当たりの放出数
    uint32_t burstCount = 0;        // バースト放出数
    float burstTime = 0.0f;      // バースト発生時間
};

/// @brief パーティクル放出モジュール（Unity Emission Module相当）
/// 放出レートとバーストの設定（時間を進めて出す数を決めるのは EmitterPlayback）
class EmissionModule : public ParticleModuleT<ParticleEmissionData> {
public:
    using EmissionData = ParticleEmissionData;

    EmissionModule();
    ~EmissionModule() = default;

#ifdef CORE_EDITOR
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui();
#endif
};
}

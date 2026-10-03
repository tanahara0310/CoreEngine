#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
/// @brief パーティクル放出モジュール（Unity Emission Module相当）
/// 放出レートとバーストの設定（時間を進めて出す数を決めるのは EmitterPlayback）
class EmissionModule : public ParticleModule {
public:
    /// @brief 放出データ
    struct EmissionData {
        uint32_t rateOverTime = 10;     // 時間当たりの放出数
        uint32_t burstCount = 0;        // バースト放出数
        float burstTime = 0.0f;      // バースト発生時間
    };

    EmissionModule();
    ~EmissionModule() = default;

    /// @brief 放出データを設定
       /// @param data 放出データ
    void SetEmissionData(const EmissionData& data) { emissionData_ = data; }

    /// @brief 放出データを取得
    /// @return 放出データの参照
    const EmissionData& GetEmissionData() const { return emissionData_; }

    /// @brief 放出データを取得（書き換え可能）
    /// @return 放出データの参照
    EmissionData& GetEmissionData() { return emissionData_; }

#ifdef CORE_EDITOR
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui();
#endif

private:
    EmissionData emissionData_;
};
}

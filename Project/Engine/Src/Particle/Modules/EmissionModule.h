#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

/// @brief パーティクル放出モジュール（Unity Emission Module相当）
/// パーティクルの放出タイミングと数を制御します

namespace CoreEngine
{
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

    /// @brief この時間で放出すべきパーティクル数を計算
    /// @param deltaTime フレーム時間
    /// @return 放出すべきパーティクル数
    uint32_t CalculateEmissionCount(float deltaTime);

    /// @brief モジュールの時間を更新
      /// @param deltaTime フレーム時間
    void UpdateTime(float deltaTime);

    /// @brief 再生を開始
    void Play();

    /// @brief 停止
    void Stop();

    /// @brief 再生中かどうか
    /// @return 再生中の場合true
    bool IsPlaying() const { return isPlaying_; }

#ifdef _DEBUG
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui() override;
#endif

private:
    EmissionData emissionData_;

    // 再生状態
    bool isPlaying_ = false;
    float elapsedTime_ = 0.0f;
    bool hasBurst_ = false;

    // Rate over Time用の累積時間
    float emissionAccumulator_ = 0.0f;
};
}

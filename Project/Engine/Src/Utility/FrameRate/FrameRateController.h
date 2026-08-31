#pragma once

#include <chrono>

namespace CoreEngine
{
/// @brief フレーム時間と FPS の計測
/// @details 計測したデルタタイムは Time へ流し込む。フレームレートの上限は Present の垂直同期に従う。
class FrameRateController {
public:
    /// @brief 初期化
    void Initialize();

    /// @brief フレーム開始時の処理
    /// @details 前フレームからの実際の経過時間を計算し、Time へ渡して FPS 計測を更新する
    void BeginFrame();

    /// @brief FPS計測をリセット（シーン切り替え時などに使用）
    /// @details 移動平均をクリアして安定した計測を再開する
    void ResetFPSMeasurement();

    /// @brief 現在のFPSを取得
    /// @return 実測FPS値（60サンプルの移動平均）
    /// @note デルタタイムは `Time::DeltaTime()` / `Time::UnscaledDeltaTime()` から取る。
    float GetCurrentFPS() const { return currentFPS_; }

    /// @brief 目標FPSを取得
    /// @return 60.0f固定
    float GetTargetFPS() const { return kTargetFPS; }

private:
    /// @brief FPS計測値を更新
    void UpdateFPSCalculation();

private:
    // 固定値
    static constexpr float kTargetFPS = 60.0f;                    // 目標FPS
    static constexpr float kFixedDeltaTime = 1.0f / kTargetFPS;   // 固定デルタタイム（フォールバック用）
    static constexpr int kFPSSampleCount = 60;                    // FPS計測用サンプル数（1秒分）
    static constexpr int kWarmupFrames = 3;                       // 初期化後の安定化フレーム数

    // 時間管理
    std::chrono::high_resolution_clock::time_point lastFrameTime_;  // 前フレームの開始時刻
    float deltaTime_ = kFixedDeltaTime;                            // フレーム間経過時間（秒）

    // FPS計測
    float fpsSamples_[kFPSSampleCount] = {};    // FPS計測用サンプル配列
    int fpsSampleIndex_ = 0;                    // 現在のサンプルインデックス
    int validSampleCount_ = 0;                  // 有効なサンプル数（初期化中は<60）
    float currentFPS_ = kTargetFPS;             // 現在のFPS（移動平均）
    int frameCount_ = 0;                        // フレームカウンター（初期化・リセット後）
};
}

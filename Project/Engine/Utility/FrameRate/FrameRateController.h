#pragma once

#include <chrono>

/// @brief フレームレート管理クラス（VSync 60FPS固定）
/// @details VSyncによる60FPS固定を前提とし、デルタタイムとFPS計測のみを行う

namespace CoreEngine
{
class FrameRateController {
public:
    /// @brief 初期化
    void Initialize();

    /// @brief フレーム開始時の処理
    /// @details 前フレームからの実際の経過時間を計算し、FPS計測を更新
    void BeginFrame();

    /// @brief FPS計測をリセット（シーン切り替え時などに使用）
    /// @details 移動平均をクリアして安定した計測を再開する
    void ResetFPSMeasurement();

    /// @brief フレーム間の経過時間を取得（秒）
    /// @return deltaTime（秒） - 実測値（VSync有効時は約0.0167秒）
    float GetDeltaTime() const { return deltaTime_; }

    /// @brief 現在のFPSを取得
    /// @return 実測FPS値（60サンプルの移動平均）
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

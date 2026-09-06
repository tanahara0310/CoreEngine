#pragma once

#include <cstdint>

namespace CoreEngine
{
/// @brief フレーム時間の取得口。実測値は FrameRateController が毎フレーム流し込む。
/// @note 更新も参照もメインスレッドから行う。
class Time {
public:
    /// @brief 固定ステップ幅と、計測が始まる前に返すデルタタイムの既定値（秒）
    static constexpr float kDefaultFixedDeltaTime = 1.0f / 60.0f;

    // ===== 経過時間 =====

    /// @brief timeScale を掛けた前フレームからの経過時間（秒）
    /// @note ゲーム世界を進める処理はこちらを使う。
    static float DeltaTime() { return deltaTime_; }

    /// @brief timeScale を掛けない実測の経過時間（秒）
    /// @note UI・エディタカメラ・計測表示など、ポーズやスローの影響を受けない処理はこちら。
    static float UnscaledDeltaTime() { return unscaledDeltaTime_; }

    /// @brief 起動からの累積時間（timeScale 適用後・秒）
    static float TimeSinceStartup() { return timeSinceStartup_; }

    /// @brief 起動からの累積時間（timeScale 非適用・秒）
    static float UnscaledTimeSinceStartup() { return unscaledTimeSinceStartup_; }

    /// @brief 起動からの経過フレーム数
    static uint64_t FrameCount() { return frameCount_; }

    // ===== 時間の速さ =====

    /// @brief 時間の進む速さ（1.0 = 等速 / 0.0 = 停止）
    static float TimeScale() { return timeScale_; }

    /// @brief 時間の進む速さを設定する（負値は 0 に丸める）
    static void SetTimeScale(float scale) { timeScale_ = (scale > 0.0f) ? scale : 0.0f; }

    // ===== ポーズ（再生 / 停止ボタン） =====

    /// @brief ゲーム時間を止めているか
    static bool IsPaused() { return paused_; }

    /// @brief ゲーム時間の停止を切り替える
    /// @param paused 止めるなら true
    /// @note 止めている間 DeltaTime() は 0 を返す。timeScale は書き換えないので、
    ///       解除するとヒットストップ等で設定された速さがそのまま戻る。
    ///       UnscaledDeltaTime() は実測のまま流れ続ける（エディタ・UI 用）。
    /// @note 切り替えるのは PlaybackStateManager の役目。ゲームコードから直接呼ぶと
    ///       メニューバーのボタンの状態と食い違う。
    static void SetPaused(bool paused) { paused_ = paused; }

    // ===== 固定ステップ =====

    /// @brief 固定ステップ 1 回分の時間（秒）
    static float FixedDeltaTime() { return fixedDeltaTime_; }

    /// @brief 固定ステップ 1 回分の時間を設定する（0 以下は無視する）
    static void SetFixedDeltaTime(float seconds)
    {
        if (seconds > 0.0f) { fixedDeltaTime_ = seconds; }
    }

    // ===== 更新 =====

    /// @brief 実測の経過時間を 1 フレーム分進める
    /// @note FrameRateController::BeginFrame() から毎フレーム 1 回だけ呼ぶ。
    static void Advance(float unscaledDeltaTime)
    {
        unscaledDeltaTime_ = unscaledDeltaTime;
        deltaTime_ = paused_ ? 0.0f : unscaledDeltaTime * timeScale_;
        unscaledTimeSinceStartup_ += unscaledDeltaTime_;
        timeSinceStartup_ += deltaTime_;
        ++frameCount_;
    }

    /// @brief 累積時間とフレーム数を初期状態へ戻す（timeScale・ポーズ・固定ステップ幅は保持する）
    static void Reset()
    {
        deltaTime_ = paused_ ? 0.0f : kDefaultFixedDeltaTime;
        unscaledDeltaTime_ = kDefaultFixedDeltaTime;
        timeSinceStartup_ = 0.0f;
        unscaledTimeSinceStartup_ = 0.0f;
        frameCount_ = 0;
    }

private:
    static inline float    deltaTime_ = kDefaultFixedDeltaTime;
    static inline float    unscaledDeltaTime_ = kDefaultFixedDeltaTime;
    static inline float    timeSinceStartup_ = 0.0f;
    static inline float    unscaledTimeSinceStartup_ = 0.0f;
    static inline float    timeScale_ = 1.0f;
    static inline bool     paused_ = false;
    static inline float    fixedDeltaTime_ = kDefaultFixedDeltaTime;
    static inline uint64_t frameCount_ = 0;
};
}

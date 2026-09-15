#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief フレーム時間の取得口をスクリプトの名前空間 `Time` へ登録する
    /// @details `Time::DeltaTime()` / `UnscaledDeltaTime()` / `TimeSinceStartup()` /
    ///          `UnscaledTimeSinceStartup()` / `FrameCount()` / `TimeScale()` / `IsPaused()`。
    ///          時間の速さと停止の切り替えは登録しない。
    /// @return すべて登録できたら true
    bool RegisterTimeBinding(asIScriptEngine* engine);
}

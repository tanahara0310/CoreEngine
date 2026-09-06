#pragma once

#include <string>

/// @file
/// @brief シーケンスのイベントトラックから EventBus へ流れる通知

namespace CoreEngine
{
    /// @brief シーケンスの Callback イベントが発火したときに流れる通知
    ///
    /// @details
    /// エディタで置いたイベントを、エンジンが知らないゲーム固有の演出へ繋ぐための逃げ道。
    /// シェイクや時間スケールのように受け手がエンジン内で決まっているものと違い、
    /// これは「誰が拾うか」をゲーム側に委ねる。
    ///
    /// @code
    ///     subscriptions_.Add(EventBus::GetInstance().Subscribe<CameraSequenceCallbackEvent>(
    ///         [](const CameraSequenceCallbackEvent& e) {
    ///             if (e.name == "汽笛") { PlaySound("whistle"); }
    ///         }));
    /// @endcode
    struct CameraSequenceCallbackEvent {
        /// @brief イベントに付けた名前（エディタで入力したもの）
        std::string name;

        /// @brief 付随する数値（用途は受け手が決める）
        float value = 0.0f;
    };
}

#pragma once

#include <cstddef>
#include <cstdint>

namespace CoreEngine
{
    /// @brief 音のカテゴリ（ミキサーバス）
    /// @details 各バスは XAudio2 のサブミックスボイス 1 本に対応し、そこからマスターへ
    ///          合流する。「BGM だけ絞る」「SE だけミュート」がバス 1 本の音量操作で済み、
    ///          そこに繋がっている音の本数に依存しない。
    /// @note バスを増やすときはここへ 1 つ足すだけでよい
    ///       （kAudioBusCount は Count から自動で求まる）。
    enum class AudioBus : uint32_t {
        BGM = 0, ///< 背景音楽
        SE,      ///< 効果音
        Voice,   ///< ボイス

        Count,   ///< バス総数を求めるための番兵。バスとして指定しないこと
    };

    /// @brief バスの総数
    inline constexpr size_t kAudioBusCount = static_cast<size_t>(AudioBus::Count);
}

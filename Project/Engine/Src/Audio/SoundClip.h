#pragma once

#include <cstdint>

#include "Audio/AudioBus.h"

namespace CoreEngine
{
    class AudioSystem;

    /// @brief デコード済み PCM を指す軽量ハンドル
    /// @details 波形の実体は AudioSystem がキャッシュとして所有する。同じパスを何度
    ///          LoadClip() しても PCM は 1 つしか載らないので、このハンドルをコピーしても
    ///          波形が複製されることはない。
    /// @note 既定構築したものは無効クリップ（読み込み失敗も同じ値が返る）。
    class SoundClip {
    public:
        SoundClip() = default;

        /// @brief 読み込みに成功したクリップを指しているか
        bool IsValid() const { return id_ != 0; }
        explicit operator bool() const { return IsValid(); }

        bool operator==(const SoundClip&) const = default;

    private:
        friend class AudioSystem;
        explicit SoundClip(uint32_t id) : id_(id) {}

        uint32_t id_ = 0; ///< AudioSystem 内のクリップ ID（0 は無効）
    };

    /// @brief 再生開始時に決めるパラメータ
    /// @details designated initializer で必要なものだけ書ける
    ///          （例: `{ .loop = true, .volume = 0.5f }`）。
    struct PlayParams {
        /// @brief 出力先のバス
        /// @note 既定は SE。BGM は必ず明示すること（バス音量とシーン遷移の
        ///       ダッキングが BGM バスに掛かるため）。
        AudioBus bus = AudioBus::SE;

        bool  loop       = false; ///< ループ再生するか
        float volume     = 1.0f;  ///< 音量（0.0〜1.0）
        float pitch      = 1.0f;  ///< ピッチ（再生速度倍率）
        float fadeInTime = 0.0f;  ///< >0 なら音量 0 から volume へフェードインする秒数
    };
}

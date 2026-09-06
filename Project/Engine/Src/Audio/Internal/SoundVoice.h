#pragma once

#include <xaudio2.h>

namespace CoreEngine
{
    struct AudioData;

    /// @brief 1 音分の再生を担当する XAudio2 ソースボイスのラッパー
    /// @note AudioSystem の内部実装。ゲーム側は SoundInstance を使うこと。
    class SoundVoice {
    public:
        SoundVoice() = default;
        ~SoundVoice();

        SoundVoice(const SoundVoice&) = delete;
        SoundVoice& operator=(const SoundVoice&) = delete;

        /// @brief 波形データからソースボイスを作る
        /// @param outputBus 出力先バスのサブミックス。nullptr ならマスターへ直結する
        /// @warning audioData はこのボイスが壊れるまで生存していること
        ///          （XAUDIO2_BUFFER が PCM を直接指すので、先に消すと解放済み領域を読む）
        bool Initialize(IXAudio2* xAudio2, const AudioData& audioData, IXAudio2SubmixVoice* outputBus);

        /// @brief 先頭から再生を開始する
        void Play(bool loop);
        /// @brief 再生を停止してバッファを空にする
        void Stop();
        /// @brief 再生位置を保ったまま一時停止する
        void Pause();
        /// @brief 一時停止した位置から再開する
        void Resume();

        void SetVolume(float volume);
        float GetVolume() const { return volume_; }
        void SetPitch(float pitch);
        float GetPitch() const { return pitch_; }

        bool IsPlaying() const;
        bool IsPaused() const { return isPaused_; }

        /// @brief 再生し切ってバッファが空になったか
        /// @details ループ中と一時停止中は false。AudioSystem がスロット回収の判定に使う。
        bool HasFinished() const;

    private:
        IXAudio2SourceVoice* sourceVoice_ = nullptr;
        XAUDIO2_BUFFER buffer_{};
        bool isPaused_ = false;
        float volume_ = 1.0f;
        float pitch_ = 1.0f;
    };
}

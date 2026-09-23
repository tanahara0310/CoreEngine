#include "pch.h"
#include "SoundVoice.h"

#include <algorithm>

#include "Audio/Decoder/AudioData.h"

#pragma comment(lib, "xaudio2.lib")

namespace CoreEngine
{
    SoundVoice::~SoundVoice()
    {
        if (sourceVoice_) {
            sourceVoice_->Stop();
            sourceVoice_->FlushSourceBuffers();
            sourceVoice_->DestroyVoice();
            sourceVoice_ = nullptr;
        }
    }

    bool SoundVoice::Initialize(IXAudio2* xAudio2, const AudioData& audioData, IXAudio2SubmixVoice* outputBus)
    {
        if (!xAudio2 || !audioData.IsValid()) {
            return false;
        }

        // 出力先をバスのサブミックスへ固定する。指定を省くと XAudio2 が
        // マスタリングボイスへ直結してしまい、バス音量が効かなくなる
        XAUDIO2_SEND_DESCRIPTOR send{ 0, outputBus };
        XAUDIO2_VOICE_SENDS sends{ 1, &send };

        const HRESULT hr = xAudio2->CreateSourceVoice(
            &sourceVoice_, audioData.Format(), 0, XAUDIO2_DEFAULT_FREQ_RATIO,
            nullptr, outputBus ? &sends : nullptr, nullptr);
        if (FAILED(hr)) {
            sourceVoice_ = nullptr;
            return false;
        }

        sourceChannels_ = audioData.Format()->nChannels;
        destinationChannels_ = 2;
        if (outputBus) {
            XAUDIO2_VOICE_DETAILS details{};
            outputBus->GetVoiceDetails(&details);
            destinationChannels_ = details.InputChannels;
        }

        buffer_.pAudioData = audioData.pcm.get();
        buffer_.AudioBytes = audioData.pcmSize;
        buffer_.Flags = XAUDIO2_END_OF_STREAM;

        sourceVoice_->SetVolume(volume_);
        sourceVoice_->SetFrequencyRatio(pitch_);
        return true;
    }

    void SoundVoice::Play(bool loop)
    {
        if (!sourceVoice_) {
            return;
        }

        // 鳴っている途中でも先頭から鳴らし直せるよう、一度空にしてから積む
        sourceVoice_->Stop();
        sourceVoice_->FlushSourceBuffers();

        buffer_.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

        if (SUCCEEDED(sourceVoice_->SubmitSourceBuffer(&buffer_))) {
            sourceVoice_->Start();
        }
        isPaused_ = false;
    }

    void SoundVoice::Stop()
    {
        if (!sourceVoice_) {
            return;
        }

        sourceVoice_->Stop();
        sourceVoice_->FlushSourceBuffers();
        isPaused_ = false;
    }

    void SoundVoice::Pause()
    {
        // 鳴り終わった音を「一時停止中」にしない。
        // 旧実装は一度 Play すると isPlaying_ が下りず、終了済みの音が
        // IsPaused() == true を名乗り、Resume() が空のボイスを Start() していた
        if (!sourceVoice_ || isPaused_ || !IsPlaying()) {
            return;
        }

        sourceVoice_->Stop();
        isPaused_ = true;
    }

    void SoundVoice::Resume()
    {
        if (!sourceVoice_ || !isPaused_) {
            return;
        }

        sourceVoice_->Start();
        isPaused_ = false;
    }

    void SoundVoice::SetVolume(float volume)
    {
        // ボイスの有無に関わらず値は保持する（Set した値を Get が返す）
        volume_ = std::clamp(volume, 0.0f, 1.0f);
        if (sourceVoice_) {
            sourceVoice_->SetVolume(volume_);
        }
    }

    void SoundVoice::SetPitch(float pitch)
    {
        // 上限は CreateSourceVoice へ渡した MaxFrequencyRatio。それを超える値を
        // SetFrequencyRatio に投げても XAudio2 側で頭打ちになるので、
        // Set した値と Get で返る値がズレないよう入口で丸めておく
        pitch_ = std::clamp(pitch, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_DEFAULT_FREQ_RATIO);
        if (sourceVoice_) {
            sourceVoice_->SetFrequencyRatio(pitch_);
        }
    }

    void SoundVoice::SetPan(float pan)
    {
        pan_ = std::clamp(pan, -1.0f, 1.0f);

        // 2 ch 以外へ出しているときと 3 ch 以上の音源は、振り分けようがないので触らない
        if (!sourceVoice_ || destinationChannels_ != 2 || sourceChannels_ < 1 || sourceChannels_ > 2) {
            return;
        }

        // 中央で左右とも 1.0（XAudio2 の既定と同じ）になる配分にする。
        // 二乗和を 1 に保つ配分にすると、中央に置いただけで音が小さくなる
        const float left = std::clamp(1.0f - pan_, 0.0f, 1.0f);
        const float right = std::clamp(1.0f + pan_, 0.0f, 1.0f);

        // 行列の並びは pLevelMatrix[SourceChannels * 出力ch + 入力ch]
        if (sourceChannels_ == 1) {
            const float matrix[2] = { left, right };
            sourceVoice_->SetOutputMatrix(nullptr, 1, 2, matrix);
        }
        else {
            // ステレオ音源は左右の分かれをそのままに、全体を寄せる
            const float matrix[4] = { left, 0.0f, 0.0f, right };
            sourceVoice_->SetOutputMatrix(nullptr, 2, 2, matrix);
        }
    }

    bool SoundVoice::IsPlaying() const
    {
        if (!sourceVoice_ || isPaused_) {
            return false;
        }

        XAUDIO2_VOICE_STATE state{};
        sourceVoice_->GetState(&state);
        return state.BuffersQueued > 0;
    }

    bool SoundVoice::HasFinished() const
    {
        if (!sourceVoice_) {
            return true;
        }
        // 一時停止はバッファを積んだまま止めているだけなので「終わっていない」
        if (isPaused_) {
            return false;
        }

        XAUDIO2_VOICE_STATE state{};
        sourceVoice_->GetState(&state);
        return state.BuffersQueued == 0;
    }
}

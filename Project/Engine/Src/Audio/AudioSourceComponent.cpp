#include "pch.h"
#include "Audio/AudioSourceComponent.h"

#include "Audio/AudioListenerComponent.h"
#include "Audio/AudioSystem.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

REFLECT_REGISTER(CoreEngine::AudioSourceComponent)
COMPONENT_REGISTER(CoreEngine::AudioSourceComponent)

namespace CoreEngine
{
    void AudioSourceComponent::Start()
    {
        // 聞き手との位置関係を先に求めておく。鳴らしてから合わせると、
        // 遠くの音が 1 フレームだけ手元で鳴ってしまう
        UpdateSpatial();
        if (playOnAwake_) {
            Play();
        }
    }

    void AudioSourceComponent::Update()
    {
        UpdateSpatial();

        SoundInstance& instance = sound_.Get();
        if (!instance.IsValid()) {
            return;
        }
        // 鳴っている間に値が変わっても追いつくようにする（インスペクタの編集とスクリプトの両方）
        if (!instance.IsFading()) {
            instance.SetVolume(EffectiveVolume());
        }
        instance.SetPitch(pitch_);
        instance.SetPan(pan_);
    }

    void AudioSourceComponent::Play()
    {
        AudioSystem* const audio = ResolveAudio();
        if (!audio) {
            return;
        }
        if (!clip_.IsSet()) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Audio,
                "{} の音の再生は、鳴らす音を指していません",
                GetOwner() ? GetOwner()->GetName() : "名前なし");
            return;
        }

        // 前の再生は代入で止まる（ScopedSound の破棄）
        sound_ = audio->PlayScoped(clip_.GetPath(), BuildParams());
        sound_.Get().SetPan(pan_);
    }

    void AudioSourceComponent::Stop()
    {
        sound_.Get().Stop();
    }

    void AudioSourceComponent::Pause()
    {
        sound_.Get().Pause();
    }

    void AudioSourceComponent::UnPause()
    {
        sound_.Get().Resume();
    }

    void AudioSourceComponent::PlayOneShot()
    {
        AudioSystem* const audio = ResolveAudio();
        if (!audio || !clip_.IsSet()) {
            return;
        }
        // 止める口を持たない鳴らし方なので、左右の振り分けは掛けられない
        audio->PlayOneShot(clip_.GetPath(), BuildParams());
    }

    void AudioSourceComponent::FadeOut(float duration)
    {
        sound_.Get().FadeOut(duration);
    }

    void AudioSourceComponent::SetVolume(float volume)
    {
        volume_ = std::clamp(volume, 0.0f, 1.0f);
    }

    void AudioSourceComponent::SetPitch(float pitch)
    {
        pitch_ = std::clamp(pitch, 0.5f, 2.0f);
    }

    void AudioSourceComponent::SetMute(bool mute)
    {
        mute_ = mute;
    }

    void AudioSourceComponent::SetSpatialBlend(float blend)
    {
        spatialBlend_ = std::clamp(blend, 0.0f, 1.0f);
    }

    AudioSystem* AudioSourceComponent::ResolveAudio() const
    {
        if (audio_) {
            return audio_;
        }
        const GameObject* const owner = GetOwner();
        EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
        audio_ = engine ? engine->GetService<AudioSystem>() : nullptr;
        return audio_;
    }

    PlayParams AudioSourceComponent::BuildParams() const
    {
        PlayParams params;
        params.bus = bus_;
        params.loop = loop_;
        params.volume = EffectiveVolume();
        params.pitch = pitch_;
        params.fadeInTime = fadeInTime_;
        return params;
    }

    void AudioSourceComponent::UpdateSpatial()
    {
        const AudioListenerComponent* const listener = AudioListenerComponent::GetActive();
        const GameObject* const owner = GetOwner();
        if (spatialBlend_ <= 0.0f || !listener || !owner) {
            if (spatialBlend_ > 0.0f && !listener && !warnedNoListener_) {
                warnedNoListener_ = true;
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Audio,
                    "{} は距離で音量を変える設定ですが、シーンに音の聞き手がいません"
                    "（カメラに AudioListener を付けてください）",
                    owner ? owner->GetName() : "名前なし");
            }
            attenuation_ = 1.0f;
            pan_ = 0.0f;
            return;
        }

        const Vector3 toSource = owner->GetWorldPosition() - listener->GetWorldPosition();
        const float distance = std::sqrt(
            toSource.x * toSource.x + toSource.y * toSource.y + toSource.z * toSource.z);

        // 減衰の始まりまでは下がらず、限界で 0 になるまで直線で下げる
        float falloff = 1.0f;
        const float span = maxDistance_ - minDistance_;
        if (distance >= maxDistance_ && span > 0.0f) {
            falloff = 0.0f;
        }
        else if (distance > minDistance_ && span > 0.0f) {
            falloff = (maxDistance_ - distance) / span;
        }

        // 「距離で変える」が途中の値なら、変えない側との間を取る
        attenuation_ = 1.0f + spatialBlend_ * (falloff - 1.0f);

        pan_ = 0.0f;
        if (distance > 0.0001f) {
            const Vector3 right = listener->GetRightAxis();
            const float side =
                (toSource.x * right.x + toSource.y * right.y + toSource.z * right.z) / distance;
            pan_ = std::clamp(side, -1.0f, 1.0f) * spatialBlend_;
        }
    }
}

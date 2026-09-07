#include "pch.h"
#include "SoundInstance.h"

#include "Audio/AudioSystem.h"
#include "Audio/Internal/SoundVoice.h"

namespace CoreEngine
{
    // 各メソッドの頭にある ResolveSlot() は「owner が居て、かつ世代が一致する」ときだけ
    // 成功する。鳴り終わった音や Stop 済みの音を触っても nullptr が返るだけなので、
    // 呼び出し側に IsValid() チェックを強制しない。

    void SoundInstance::Stop()
    {
        auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr;
        if (slot) {
            slot->voice->Stop();
            owner_->ReleaseSlot(index_);
        }

        // 自分のハンドルも落としておく。以降 owner_ を辿らないので、
        // AudioSystem が先に壊れても安全になる
        *this = {};
    }

    void SoundInstance::Pause()
    {
        if (auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr) {
            slot->voice->Pause();
        }
    }

    void SoundInstance::Resume()
    {
        if (auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr) {
            slot->voice->Resume();
        }
    }

    void SoundInstance::SetVolume(float volume)
    {
        if (auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr) {
            slot->fading = false; // 手で音量を触ったらフェードは打ち切る
            slot->voice->SetVolume(volume);
        }
    }

    float SoundInstance::GetVolume() const
    {
        const auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr;
        return slot ? slot->voice->GetVolume() : 0.0f;
    }

    void SoundInstance::SetPitch(float pitch)
    {
        if (auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr) {
            slot->voice->SetPitch(pitch);
        }
    }

    float SoundInstance::GetPitch() const
    {
        const auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr;
        return slot ? slot->voice->GetPitch() : 1.0f;
    }

    bool SoundInstance::IsPlaying() const
    {
        const auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr;
        return slot && slot->voice->IsPlaying();
    }

    bool SoundInstance::IsPaused() const
    {
        const auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr;
        return slot && slot->voice->IsPaused();
    }

    void SoundInstance::FadeTo(float targetVolume, float duration, bool stopAfterFade)
    {
        if (auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr) {
            owner_->StartFade(*slot, targetVolume, duration, stopAfterFade);
        }
    }

    void SoundInstance::FadeIn(float duration, float targetVolume)
    {
        if (auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr) {
            slot->voice->SetVolume(0.0f);
            owner_->StartFade(*slot, targetVolume, duration, false);
        }
    }

    void SoundInstance::FadeOut(float duration, bool stopAfterFade)
    {
        FadeTo(0.0f, duration, stopAfterFade);
    }

    bool SoundInstance::IsFading() const
    {
        const auto* slot = owner_ ? owner_->ResolveSlot(index_, generation_) : nullptr;
        return slot && slot->fading;
    }

    bool SoundInstance::IsValid() const
    {
        return owner_ && owner_->ResolveSlot(index_, generation_) != nullptr;
    }
}

#include "pch.h"
#include "Script/Binding/AudioBinding.h"

#include "Audio/AudioSystem.h"
#include "Script/Binding/BindingRegistrar.h"

#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace CoreEngine::Script
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<PlayParams>);

        /// 鳴らす先（登録時に受け取る）
        AudioSystem* sAudio = nullptr;

        /// @brief スクリプトへ渡す再生のハンドル（最後の参照が消えると止まる）
        class ScriptSound
        {
        public:
            explicit ScriptSound(ScopedSound sound) : sound_(std::move(sound)) {}

            ScriptSound(const ScriptSound&) = delete;
            ScriptSound& operator=(const ScriptSound&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            void Stop() { sound_.Get().Stop(); }
            void Pause() { sound_.Get().Pause(); }
            void Resume() { sound_.Get().Resume(); }
            float GetVolume() const { return sound_.Get().GetVolume(); }
            void SetVolume(float volume) { sound_.Get().SetVolume(volume); }
            float GetPitch() const { return sound_.Get().GetPitch(); }
            void SetPitch(float pitch) { sound_.Get().SetPitch(pitch); }
            void FadeTo(float targetVolume, float duration, bool stopAfterFade) { sound_.Get().FadeTo(targetVolume, duration, stopAfterFade); }
            void FadeIn(float duration, float targetVolume) { sound_.Get().FadeIn(duration, targetVolume); }
            void FadeOut(float duration, bool stopAfterFade) { sound_.Get().FadeOut(duration, stopAfterFade); }
            bool IsPlaying() const { return sound_.Get().IsPlaying(); }
            bool IsPaused() const { return sound_.Get().IsPaused(); }
            bool IsValid() const { return sound_.Get().IsValid(); }

        private:
            ~ScriptSound() = default;

            ScopedSound sound_;
            mutable int refCount_ = 1;
        };

        void ConstructPlayParams(PlayParams* self)
        {
            new (self) PlayParams();
        }

        void PlayOneShot(const std::string& path, const PlayParams& params)
        {
            if (sAudio) {
                sAudio->PlayOneShot(path, params);
            }
        }

        ScriptSound* PlayScoped(const std::string& path, const PlayParams& params)
        {
            return sAudio ? new ScriptSound(sAudio->PlayScoped(path, params)) : nullptr;
        }

        void SetBusVolume(int bus, float volume)
        {
            if (sAudio) {
                sAudio->SetBusVolume(static_cast<AudioBus>(bus), volume);
            }
        }

        float GetBusVolume(int bus)
        {
            return sAudio ? sAudio->GetBusVolume(static_cast<AudioBus>(bus)) : 0.0f;
        }

        void SetMasterVolume(float volume)
        {
            if (sAudio) {
                sAudio->SetMasterVolume(volume);
            }
        }

        float GetMasterVolume()
        {
            return sAudio ? sAudio->GetMasterVolume() : 0.0f;
        }

        void StopAll()
        {
            if (sAudio) {
                sAudio->StopAll();
            }
        }

        void RegisterParams(BindingRegistrar& r)
        {
            r.Enum("AudioBus");
            r.EnumValue("AudioBus", "BGM", static_cast<int>(AudioBus::BGM));
            r.EnumValue("AudioBus", "SE", static_cast<int>(AudioBus::SE));
            r.EnumValue("AudioBus", "Voice", static_cast<int>(AudioBus::Voice));

            r.ValueType("PlayParams", sizeof(PlayParams), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<PlayParams>());
            r.Behaviour("PlayParams", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructPlayParams), asCALL_CDECL_OBJLAST);
            r.Property("PlayParams", "AudioBus bus", asOFFSET(PlayParams, bus));
            r.Property("PlayParams", "bool loop", asOFFSET(PlayParams, loop));
            r.Property("PlayParams", "float volume", asOFFSET(PlayParams, volume));
            r.Property("PlayParams", "float pitch", asOFFSET(PlayParams, pitch));
            r.Property("PlayParams", "float fadeInTime", asOFFSET(PlayParams, fadeInTime));
        }

        void RegisterSound(BindingRegistrar& r)
        {
            r.ReferenceType("Sound", asOBJ_REF);
            r.Behaviour("Sound", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptSound, AddRef), asCALL_THISCALL);
            r.Behaviour("Sound", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptSound, Release), asCALL_THISCALL);
            r.Method("Sound", "void Stop()", asMETHOD(ScriptSound, Stop), asCALL_THISCALL);
            r.Method("Sound", "void Pause()", asMETHOD(ScriptSound, Pause), asCALL_THISCALL);
            r.Method("Sound", "void Resume()", asMETHOD(ScriptSound, Resume), asCALL_THISCALL);
            r.Method("Sound", "float get_volume() const property", asMETHOD(ScriptSound, GetVolume), asCALL_THISCALL);
            r.Method("Sound", "void set_volume(float) property", asMETHOD(ScriptSound, SetVolume), asCALL_THISCALL);
            r.Method("Sound", "float get_pitch() const property", asMETHOD(ScriptSound, GetPitch), asCALL_THISCALL);
            r.Method("Sound", "void set_pitch(float) property", asMETHOD(ScriptSound, SetPitch), asCALL_THISCALL);
            r.Method("Sound", "void FadeTo(float targetVolume, float duration, bool stopAfterFade = false)",
                asMETHOD(ScriptSound, FadeTo), asCALL_THISCALL);
            r.Method("Sound", "void FadeIn(float duration, float targetVolume = 1.0f)", asMETHOD(ScriptSound, FadeIn), asCALL_THISCALL);
            r.Method("Sound", "void FadeOut(float duration, bool stopAfterFade = true)", asMETHOD(ScriptSound, FadeOut), asCALL_THISCALL);
            r.Method("Sound", "bool get_isPlaying() const property", asMETHOD(ScriptSound, IsPlaying), asCALL_THISCALL);
            r.Method("Sound", "bool get_isPaused() const property", asMETHOD(ScriptSound, IsPaused), asCALL_THISCALL);
            r.Method("Sound", "bool get_isValid() const property", asMETHOD(ScriptSound, IsValid), asCALL_THISCALL);
        }
    }

    bool RegisterAudioBinding(asIScriptEngine* engine, AudioSystem* audio)
    {
        if (!engine) {
            return false;
        }
        sAudio = audio;

        BindingRegistrar r(engine);
        RegisterParams(r);
        RegisterSound(r);
        r.Namespace("Audio");
        r.Function("void PlayOneShot(const string &in path, const PlayParams &in params = PlayParams())", asFUNCTION(PlayOneShot));
        r.Function("Sound@ PlayScoped(const string &in path, const PlayParams &in params = PlayParams())", asFUNCTION(PlayScoped));
        r.Function("void SetBusVolume(AudioBus bus, float volume)", asFUNCTION(SetBusVolume));
        r.Function("float GetBusVolume(AudioBus bus)", asFUNCTION(GetBusVolume));
        r.Function("void SetMasterVolume(float volume)", asFUNCTION(SetMasterVolume));
        r.Function("float GetMasterVolume()", asFUNCTION(GetMasterVolume));
        r.Function("void StopAll()", asFUNCTION(StopAll));
        r.Namespace("");
        return r.Succeeded();
    }
}

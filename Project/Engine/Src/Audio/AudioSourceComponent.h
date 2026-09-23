#pragma once

#include "Audio/AudioBus.h"
#include "Audio/SoundClip.h"
#include "Audio/SoundInstance.h"
#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    class AudioSystem;

    /// @brief 音を 1 つ持って鳴らすコンポーネント
    /// @details インスペクタで音を指してチェックを入れるだけで鳴り、スクリプトからは
    ///          `owner.audioSource.Play()` のように呼べる。
    ///          「距離で変える」を上げると、`AudioListenerComponent` からの距離で
    ///          音量が下がり、左右にも振り分けられる。
    /// @note 持ち主が消えると音も止まる（`ScopedSound` が止める）。
    ///       重ねて鳴らしたい効果音は `PlayOneShot()` を使うこと。
    class AudioSourceComponent : public IComponent
    {
    public:
        /// @brief 出力先の名前（`AudioBus` の並び）
        static constexpr const char* kBusNames[] = { "BGM", "効果音", "ボイス" };

        const char* GetTypeName() const override { return "AudioSource"; }

        REFLECT_BEGIN(AudioSourceComponent, "音の再生")
            REFLECT_ASSET_REF(clip_, "音")
            REFLECT_ENUM_ACCESSOR("bus", "出力先", GetBus, SetBus, kBusNames,
                p.tooltip = "音量の設定をまとめる単位。BGM だけ絞るといった操作がここで効く")
            REFLECT_PROPERTY(playOnAwake_, "開始時に鳴らす")
            REFLECT_ACCESSOR("loop", "繰り返す", IsLoop, SetLoop)
            REFLECT_ACCESSOR("volume", "音量", GetVolume, SetVolume,
                p.range = Range(0.0f, 1.0f, 0.01f))
            REFLECT_ACCESSOR("pitch", "高さ", GetPitch, SetPitch,
                p.range = Range(0.5f, 2.0f, 0.01f),
                p.tooltip = "再生速度の倍率。上げると速く高くなる")
            REFLECT_ACCESSOR("mute", "消音", IsMuted, SetMute)
            REFLECT_PROPERTY(fadeInTime_, "フェードイン", p.range = Range(0.0f, 10.0f, 0.1f),
                p.tooltip = "鳴らし始めに音量 0 からここまで上げる秒数")
            REFLECT_ACCESSOR("spatialBlend", "距離で変える", GetSpatialBlend, SetSpatialBlend,
                p.range = Range(0.0f, 1.0f, 0.01f),
                p.tooltip = "0 で距離に関係なく同じ音量、1 で聞き手からの距離と左右を反映する")
            REFLECT_PROPERTY(minDistance_, "減衰の始まり", p.range = Range(0.0f, 1000.0f, 0.1f),
                p.tooltip = "この距離までは音量が下がらない")
            REFLECT_PROPERTY(maxDistance_, "聞こえる限界", p.range = Range(0.0f, 10000.0f, 0.1f),
                p.tooltip = "この距離で音量が 0 になる")
            REFLECT_READONLY_ACCESSOR("isPlaying", "再生中", IsPlaying)

            REFLECT_METHOD("Play", "鳴らす", Play,
                m.tooltip = "先頭から鳴らす。鳴っている途中なら鳴らし直す")
            REFLECT_METHOD("Stop", "止める", Stop)
            REFLECT_METHOD("Pause", "一時停止", Pause)
            REFLECT_METHOD("UnPause", "再開", UnPause)
            REFLECT_METHOD("PlayOneShot", "重ねて鳴らす", PlayOneShot,
                m.tooltip = "今鳴っている音を止めずにもう 1 つ鳴らす。止められない")
            REFLECT_METHOD("FadeOut", "フェードアウト", FadeOut)
        REFLECT_END()

        /// @brief 「開始時に鳴らす」が立っていれば鳴らす
        void Start() override;

        /// @brief 距離と左右の反映、インスペクタで変えた値の反映
        void Update() override;

        // ===== 操作 =====

        /// @brief 先頭から鳴らす（鳴っている途中なら鳴らし直す）
        void Play();

        /// @brief 止める
        void Stop();

        /// @brief 再生位置を保ったまま止める
        void Pause();

        /// @brief 一時停止した位置から再開する
        void UnPause();

        /// @brief 今鳴っている音を止めずにもう 1 つ鳴らす（止められない）
        void PlayOneShot();

        /// @brief 音量を 0 へ下げてから止める
        void FadeOut(float duration);

        /// @brief 鳴っているか（一時停止中は false）
        bool IsPlaying() const { return sound_.Get().IsPlaying(); }

        // ===== 値 =====

        AudioBus GetBus() const { return bus_; }
        void SetBus(AudioBus bus) { bus_ = bus; }

        bool IsLoop() const { return loop_; }
        void SetLoop(bool loop) { loop_ = loop; }

        float GetVolume() const { return volume_; }
        void SetVolume(float volume);

        float GetPitch() const { return pitch_; }
        void SetPitch(float pitch);

        bool IsMuted() const { return mute_; }
        void SetMute(bool mute);

        float GetSpatialBlend() const { return spatialBlend_; }
        void SetSpatialBlend(float blend);

        /// @brief 鳴らす音をパスかファイル名で指す
        void SetClip(const std::string& path) { clip_.SetPath(path); }

        /// @brief 指している音のパス（何も指していなければ空）
        std::string GetClipPath() const { return clip_.IsSet() ? clip_.GetPath() : std::string{}; }

    private:
        /// @brief 音を鳴らすサービスを引く（引けるまで毎回試す）
        AudioSystem* ResolveAudio() const;

        /// @brief 今の値から再生時のパラメータを組む
        PlayParams BuildParams() const;

        /// @brief 消音と距離の減衰を掛けた、実際に流す音量
        float EffectiveVolume() const { return mute_ ? 0.0f : volume_ * attenuation_; }

        /// @brief 聞き手との位置から減衰と左右の振り分けを求める
        void UpdateSpatial();

        AssetRef<AudioAsset> clip_;
        AudioBus bus_ = AudioBus::SE;
        bool playOnAwake_ = false;
        bool loop_ = false;
        float volume_ = 1.0f;
        float pitch_ = 1.0f;
        bool mute_ = false;
        float fadeInTime_ = 0.0f;

        float spatialBlend_ = 0.0f;
        float minDistance_ = 1.0f;
        float maxDistance_ = 50.0f;

        /// 距離から決まる倍率（「距離で変える」が 0 なら 1）
        float attenuation_ = 1.0f;
        /// 左右の寄せ（-1 で左、+1 で右）
        float pan_ = 0.0f;

        /// 聞き手が居ないことを一度だけ伝えるための印
        bool warnedNoListener_ = false;

        mutable AudioSystem* audio_ = nullptr;
        ScopedSound sound_;
    };
}

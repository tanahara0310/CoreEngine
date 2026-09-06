#pragma once

#include <cstdint>
#include <memory>

namespace CoreEngine
{
    class AudioSystem;

    /// @brief 1 回の再生を指す値型ハンドル
    /// @details コピー自由・デストラクタ無害。ハンドルは世代番号を持ち、再生が終わって
    ///          スロットが再利用された後は「別の音」と識別できるので、止まった音への
    ///          操作は黙って無視される（クラッシュしないし、他人の音も触らない）。
    /// @note 鳴らしっぱなしで良い SE は戻り値を捨ててよい。スコープと寿命を揃えたい
    ///       BGM 等には ScopedSound を被せること。
    /// @warning AudioSystem と同じスレッド（メインスレッド）からのみ操作すること。
    class SoundInstance {
    public:
        SoundInstance() = default;

        /// @brief 再生を停止してスロットを解放する（以後このハンドルは無効になる）
        void Stop();
        /// @brief 再生位置を保ったまま一時停止する
        void Pause();
        /// @brief 一時停止した位置から再開する
        void Resume();

        /// @brief 音量（0.0〜1.0）を設定。フェード中に呼ぶとフェードは打ち切られる
        void SetVolume(float volume);
        float GetVolume() const;
        /// @brief ピッチ（再生速度倍率）を設定
        void SetPitch(float pitch);
        float GetPitch() const;

        bool IsPlaying() const;
        bool IsPaused() const;

        /// @brief 現在の音量から目標音量へフェードする
        /// @param targetVolume 目標音量（0.0〜1.0）
        /// @param duration フェード時間（秒）。0 以下なら即座に反映する
        /// @param stopAfterFade フェード完了後に停止するか
        void FadeTo(float targetVolume, float duration, bool stopAfterFade = false);
        /// @brief 音量 0 から目標音量へフェードインする
        void FadeIn(float duration, float targetVolume = 1.0f);
        /// @brief 現在の音量から 0 へフェードアウトする
        void FadeOut(float duration, bool stopAfterFade = true);
        bool IsFading() const;

        /// @brief まだ生きている再生を指しているか（鳴り終わった音は false）
        bool IsValid() const;
        explicit operator bool() const { return IsValid(); }

    private:
        friend class AudioSystem;
        SoundInstance(AudioSystem* owner, uint32_t index, uint32_t generation)
            : owner_(owner), index_(index), generation_(generation) {}

        AudioSystem* owner_ = nullptr;
        uint32_t index_ = 0;
        uint32_t generation_ = 0; ///< 0 は無効ハンドル。発行される世代は必ず 1 以上
    };

    /// @brief SoundInstance をスコープに縛る RAII ラッパ
    /// @details 破棄時に Stop() する。BGM のように「シーンが所有していて、抜けたら
    ///          確実に止めたい」音のためのもの。撃ちっぱなしの SE には使わないこと
    ///          （スコープを抜けた瞬間に切れてしまう）。
    /// @note AudioSystem が先に壊れていた場合は何もしない（生存トークンで判定する）。
    class ScopedSound {
    public:
        ScopedSound() = default;
        ScopedSound(const SoundInstance& instance, std::weak_ptr<void> systemLifetime)
            : instance_(instance), systemLifetime_(std::move(systemLifetime)) {}
        ~ScopedSound() { StopIfSystemAlive(); }

        ScopedSound(const ScopedSound&) = delete;
        ScopedSound& operator=(const ScopedSound&) = delete;

        ScopedSound(ScopedSound&& other) noexcept
            : instance_(other.instance_), systemLifetime_(std::move(other.systemLifetime_)) {
            other.instance_ = {};
        }
        ScopedSound& operator=(ScopedSound&& other) noexcept {
            if (this != &other) {
                StopIfSystemAlive();
                instance_ = other.instance_;
                systemLifetime_ = std::move(other.systemLifetime_);
                other.instance_ = {};
            }
            return *this;
        }

        SoundInstance& Get() { return instance_; }
        const SoundInstance& Get() const { return instance_; }
        SoundInstance* operator->() { return &instance_; }
        const SoundInstance* operator->() const { return &instance_; }
        explicit operator bool() const { return instance_.IsValid(); }

    private:
        void StopIfSystemAlive() {
            // AudioSystem が先に破棄されていたら触らない
            if (!systemLifetime_.expired()) {
                instance_.Stop();
            }
            instance_ = {};
        }

        SoundInstance instance_;
        std::weak_ptr<void> systemLifetime_;
    };
}

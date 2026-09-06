#pragma once

#include <array>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <wrl.h>

#include "Audio/AudioBus.h"
#include "Audio/SoundClip.h"
#include "Audio/SoundInstance.h"

// XAudio2 / Media Foundation のヘッダはここでは include しない。
// このヘッダはゲーム側の全シーンから見えるので、Win32 マルチメディア一式
// （と PlaySound マクロ）を撒かないよう前方宣言だけで済ませている。
struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SubmixVoice;

namespace CoreEngine
{
    struct AudioData;
    class IAudioDecoder;
    class SoundVoice;

    /// @brief 音声の読み込み・再生を統括するエンジンサービス
    ///
    /// @details 「重くて共有できる波形（SoundClip）」と「軽くて使い捨ての再生
    ///          （SoundInstance）」を分けているのが設計の中心。同じ SE を重ねて鳴らしても
    ///          PCM は 1 つしか載らず、鳴り終わった再生は Update() が自動で回収する。
    ///
    /// @code
    ///     auto* audio = engine->GetService<AudioSystem>();
    ///
    ///     // 撃ちっぱなしの SE（重ねて鳴る）
    ///     audio->PlayOneShot("Sounds/se_jump.wav", { .volume = 0.8f });
    ///
    ///     // 制御したい音
    ///     SoundInstance se = audio->Play("Sounds/se_charge.wav");
    ///     se.FadeOut(0.5f);
    ///
    ///     // シーンが所有する BGM（メンバに置けばシーン破棄で自動停止）
    ///     ScopedSound bgm = audio->PlayScoped("Sounds/bgm.mp3",
    ///         { .bus = AudioBus::BGM, .loop = true, .fadeInTime = 1.5f });
    ///
    ///     // オプション画面（鳴っている音の本数に関係なくカテゴリ単位で効く）
    ///     audio->SetBusVolume(AudioBus::BGM, 0.4f);
    /// @endcode
    ///
    /// @warning 初期化以外はメインスレッド専用。クリップとインスタンスのテーブルに
    ///          ロックは掛けていない（初期化だけは別スレッドで走るので initMutex_ で守る）。
    class AudioSystem {
    public:
        AudioSystem();
        ~AudioSystem();

        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        // ──────────────────────────────────────────────────────
        // 初期化・終了
        // ──────────────────────────────────────────────────────

        /// @brief 初期化をワーカースレッドで開始する（完了を待たずに戻る）
        /// @details XAudio2 と Media Foundation の初期化は実測 0.676 秒かかるが CPU は
        ///          ほぼ使わない（全部待ち）。音を使う入口が内部で合流するので、
        ///          呼び出し側が完了を管理する必要はない。
        void BeginInitializeAsync();

        /// @brief 全ての音を止め、XAudio2 / Media Foundation を解放する（冪等）
        void Shutdown();

        // ──────────────────────────────────────────────────────
        // フレーム更新
        // ──────────────────────────────────────────────────────

        /// @brief 毎フレーム呼ぶ更新処理
        /// @param deltaTime 経過時間（秒）。ポーズ中もフェードを進めたいので
        ///        Time::UnscaledDeltaTime() を渡すこと
        /// @details フェードの進行と、鳴り終わった再生スロットの回収を行う。
        ///          EngineSystem::BeginFrame() から呼ばれるので、ゲーム側の呼び出しは不要。
        void Update(float deltaTime);

        // ──────────────────────────────────────────────────────
        // 読み込み
        // ──────────────────────────────────────────────────────

        /// @brief 音声ファイルを読み込む（同じパスなら PCM を共有する）
        /// @param filename ファイルパス（Assets フォルダを省略可能）
        /// @return 読み込まれたクリップ。失敗時は無効クリップ
        SoundClip LoadClip(const std::string& filename);

        /// @brief どの再生からも参照されていないクリップを解放する
        /// @warning 解放されたクリップを指していた SoundClip は無効になる
        ///          （その状態で Play() しても無音になるだけでクラッシュはしない）。
        void UnloadUnusedClips();

        // ──────────────────────────────────────────────────────
        // 再生
        // ──────────────────────────────────────────────────────

        /// @brief クリップを再生し、制御用のハンドルを返す
        /// @return 再生インスタンス。失敗時は無効ハンドル（操作しても何も起きない）
        SoundInstance Play(const SoundClip& clip, const PlayParams& params = {});

        /// @brief パス指定で再生する（内部で LoadClip する。2 回目以降はキャッシュに当たる）
        SoundInstance Play(const std::string& filename, const PlayParams& params = {});

        /// @brief 制御しない SE を鳴らす（ハンドルを持たない Play）
        void PlayOneShot(const SoundClip& clip, const PlayParams& params = {});
        void PlayOneShot(const std::string& filename, const PlayParams& params = {});

        /// @brief スコープに縛って再生する（保持している ScopedSound の破棄で自動停止）
        /// @details シーンが所有する BGM 向け。撃ちっぱなしの SE には使わないこと。
        ScopedSound PlayScoped(const SoundClip& clip, const PlayParams& params = {});
        ScopedSound PlayScoped(const std::string& filename, const PlayParams& params = {});

        /// @brief 鳴っている音を全て止める
        void StopAll();

        /// @brief 現在鳴っている（一時停止も含む）再生の数
        size_t GetPlayingCount() const;

        // ──────────────────────────────────────────────────────
        // 音量（マスター / バス）
        //
        // 実効音量 = ソース音量 × バス音量 × バスダッキング × マスター音量。
        // 「ユーザー設定（SetBusVolume）」と「演出（SetBusDuck）」を別枠にしてあるので、
        // シーン遷移が BGM を絞っても、オプション画面の設定値は上書きされない。
        // ──────────────────────────────────────────────────────

        /// @brief マスター音量（0.0〜1.0）を設定する
        /// @note 初期化前に呼んでも待たない。値を覚えておいて初期化完了時に反映する。
        void SetMasterVolume(float volume);
        float GetMasterVolume() const { return masterVolume_; }

        /// @brief バス音量（0.0〜1.0）を設定する。オプション画面の設定値はこちら
        void SetBusVolume(AudioBus bus, float volume);
        float GetBusVolume(AudioBus bus) const;

        /// @brief バスのダッキング係数（0.0〜1.0）を設定する
        /// @details 「一時的に絞る」演出用の、ユーザー設定とは別枠の倍率。
        ///          シーン遷移のフェードに同期した BGM の減衰がこれを使う。
        void SetBusDuck(AudioBus bus, float duck);
        float GetBusDuck(AudioBus bus) const;

        /// @brief ScopedSound が「この AudioSystem がまだ生きているか」を判定するためのトークン
        /// @details Shutdown() で切れる。デストラクタから破棄済みのシステムを
        ///          触らせないための仕組み。
        std::weak_ptr<void> GetLifetimeToken() const { return lifetimeToken_; }

    private:
        friend class SoundInstance;

        /// @brief 1 スロット = 1 再生インスタンス
        struct VoiceSlot {
            std::unique_ptr<SoundVoice> voice;  ///< 使用中は非 null
            uint32_t generation = 1;            ///< 発行済みハンドルの照合用（0 は無効値なので使わない）
            uint32_t clipId = 0;                ///< 参照中のクリップ（PCM の寿命判定に使う）
            bool inUse = false;

            // フェード状態（AudioSystem::Update が進める）
            bool fading = false;
            float fadeTimer = 0.0f;
            float fadeDuration = 0.0f;
            float fadeStartVolume = 0.0f;
            float fadeTargetVolume = 0.0f;
            bool stopAfterFade = false;
        };

        /// @brief クリップキャッシュの 1 エントリ
        struct ClipEntry {
            std::unique_ptr<AudioData> data;
            std::filesystem::path path;
        };

        /// @brief 1 バス = 1 サブミックスボイス
        struct Bus {
            IXAudio2SubmixVoice* submix = nullptr; ///< 初期化完了後は非 null
            float volume = 1.0f;                   ///< ユーザー設定音量
            float duck = 1.0f;                     ///< 演出用の一時的な減衰
        };

        // ── 初期化 ──
        bool EnsureInitialized();
        bool InitializeInternal();
        bool InitializeMediaFoundation();
        bool CreateBuses();
        void ShutdownMediaFoundation();

        /// @brief バスの実効音量をサブミックスへ反映する
        /// @warning initMutex_ を保持した状態で呼ぶこと（submix は初期化スレッドが書く）
        void ApplyBusGainLocked(Bus& bus);

        /// @brief バス番号から実体を引く（番兵や範囲外は SE 扱い）
        Bus& BusOf(AudioBus bus);
        const Bus& BusOf(AudioBus bus) const;

        // ── スロット管理 ──
        uint32_t AcquireSlot();
        void ReleaseSlot(uint32_t index);
        VoiceSlot* ResolveSlot(uint32_t index, uint32_t generation);
        const VoiceSlot* ResolveSlot(uint32_t index, uint32_t generation) const;

        // ── フェード ──
        void StartFade(VoiceSlot& slot, float targetVolume, float duration, bool stopAfterFade);
        void AdvanceFade(uint32_t index, float deltaTime);

        // ── デコード ──
        std::unique_ptr<AudioData> DecodeFile(const std::filesystem::path& path) const;

        // XAudio2
        Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
        IXAudio2MasteringVoice* masteringVoice_ = nullptr;
        bool mfInitialized_ = false;
        float masterVolume_ = 1.0f;

        // ミキサーバス（SourceVoice → Submix → MasteringVoice）
        std::array<Bus, kAudioBusCount> buses_{};

        // 非同期初期化の状態
        std::future<bool> initFuture_;
        mutable std::mutex initMutex_;
        bool initCompleted_ = false;
        bool initSucceeded_ = false;

        // クリップキャッシュ（波形は AudioSystem の寿命 or UnloadUnusedClips() まで残る）
        std::unordered_map<uint32_t, ClipEntry> clips_;
        std::unordered_map<std::wstring, uint32_t> clipIdByPath_;
        uint32_t nextClipId_ = 1; ///< 0 は無効クリップに予約

        // 再生スロット
        std::vector<VoiceSlot> slots_;
        std::vector<uint32_t> freeSlots_;

        // デコーダ（対応形式を増やすときはここへ 1 つ足す）
        std::vector<std::unique_ptr<IAudioDecoder>> decoders_;

        /// @brief 生存トークンの実体。Shutdown() で reset して weak_ptr を切る
        std::shared_ptr<void> lifetimeToken_;
    };
}

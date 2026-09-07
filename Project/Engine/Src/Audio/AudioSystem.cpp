#include "pch.h"
#include "AudioSystem.h"

#include <algorithm>
#include <format>
#include <unordered_set>
#include <utility>

#include <xaudio2.h>
#include <mfapi.h>

#include "Audio/Decoder/AudioData.h"
#include "Audio/Decoder/MediaFoundationDecoder.h"
#include "Audio/Decoder/WavDecoder.h"
#include "Audio/Internal/SoundVoice.h"
#include "Audio/Path/AudioPathResolver.h"
#include "Utility/Logger/Logger.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")

namespace CoreEngine
{
    // ──────────────────────────────────────────────────────────
    // 生成・破棄
    // ──────────────────────────────────────────────────────────

    AudioSystem::AudioSystem()
        : lifetimeToken_(std::make_shared<char>())
    {
        // 対応形式を増やすときはここへ 1 つ足す（拡張子の判定は各デコーダが持つ）
        decoders_.push_back(std::make_unique<WavDecoder>());
        decoders_.push_back(std::make_unique<MediaFoundationDecoder>());
    }

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    // ──────────────────────────────────────────────────────────
    // 初期化・終了
    // ──────────────────────────────────────────────────────────

    void AudioSystem::BeginInitializeAsync()
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        if (initCompleted_ || initFuture_.valid()) {
            return;
        }

        initFuture_ = std::async(std::launch::async, [this]() {
            // XAudio2 と Media Foundation はどちらも COM を要求する。
            // メインスレッドの CoInitializeEx はこのスレッドには効かないので自分で初期化する。
            // CoUninitialize は呼ばない: MFStartup が握った参照をこのスレッドの終了時に
            // 落とすと、以降に別スレッドから MF を使う経路が壊れうる
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            return InitializeInternal();
            });
    }

    bool AudioSystem::EnsureInitialized()
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        if (initCompleted_) {
            return initSucceeded_;
        }

        if (initFuture_.valid()) {
            // 非同期初期化が進行中／完了済み。ここで合流する
            initSucceeded_ = initFuture_.get();
        } else {
            // BeginInitializeAsync を経ずに使われた経路（テスト等）は同期で初期化する
            initSucceeded_ = InitializeInternal();
        }
        initCompleted_ = true;

        // 初期化前に設定された音量をここで反映する。
        // 初期化スレッドは既に合流済みなので、ボイスへ安全に触れる
        if (initSucceeded_) {
            if (masteringVoice_) {
                masteringVoice_->SetVolume(masterVolume_);
            }
            for (auto& bus : buses_) {
                ApplyBusGainLocked(bus);
            }
        }
        return initSucceeded_;
    }

    bool AudioSystem::InitializeInternal()
    {
        if (FAILED(XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}", "XAudio2Create failed");
            return false;
        }

        if (FAILED(xAudio2_->CreateMasteringVoice(&masteringVoice_))) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}", "CreateMasteringVoice failed");
            return false;
        }

        if (!CreateBuses()) {
            return false;
        }

        return InitializeMediaFoundation();
    }

    bool AudioSystem::CreateBuses()
    {
        // サブミックスの入力仕様はマスタリングボイスに合わせる。
        // ソースボイス側のサンプルレート差は XAudio2 が自動でリサンプルする
        XAUDIO2_VOICE_DETAILS details{};
        masteringVoice_->GetVoiceDetails(&details);

        for (auto& bus : buses_) {
            if (FAILED(xAudio2_->CreateSubmixVoice(&bus.submix, details.InputChannels, details.InputSampleRate))) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}", "CreateSubmixVoice failed");
                return false;
            }
        }

        // 音量の反映は EnsureInitialized() の合流後に行う。
        // ここは初期化スレッドなので、メインスレッドが触る値を読みに行かない
        return true;
    }

    bool AudioSystem::InitializeMediaFoundation()
    {
        if (FAILED(MFStartup(MF_VERSION))) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}", "MFStartup failed");
            return false;
        }

        mfInitialized_ = true;
        return true;
    }

    void AudioSystem::ShutdownMediaFoundation()
    {
        if (mfInitialized_) {
            MFShutdown();
            mfInitialized_ = false;
        }
    }

    void AudioSystem::Shutdown()
    {
        // 非同期初期化の最中に壊すと XAudio2 / Media Foundation が中途半端に残るので合流する。
        // EnsureInitialized() は使わない（未開始なら同期実行してしまい、
        // 一度も使われなかった AudioSystem の破棄時に無意味な初期化が走る）
        {
            std::lock_guard<std::mutex> lock(initMutex_);
            if (!initCompleted_ && initFuture_.valid()) {
                initSucceeded_ = initFuture_.get();
                initCompleted_ = true;
            }
        }

        // 必ず「ボイス → PCM」の順で壊す。逆にすると再生中のボイスが
        // 解放済みバッファを指したまま残る
        slots_.clear();
        freeSlots_.clear();
        clips_.clear();
        clipIdByPath_.clear();

        // 下流（マスター）より先に上流（サブミックス）を壊す
        for (auto& bus : buses_) {
            if (bus.submix) {
                bus.submix->DestroyVoice();
                bus.submix = nullptr;
            }
        }

        if (masteringVoice_) {
            masteringVoice_->DestroyVoice();
            masteringVoice_ = nullptr;
        }
        xAudio2_.Reset();
        ShutdownMediaFoundation();

        // 生存トークンを切る。ScopedSound のデストラクタが
        // 破棄済みの AudioSystem を触るのを防ぐ
        lifetimeToken_.reset();
    }

    // ──────────────────────────────────────────────────────────
    // フレーム更新
    // ──────────────────────────────────────────────────────────

    void AudioSystem::Update(float deltaTime)
    {
        // 初期化前は再生スロットが 1 つも無い。ここで初期化を待つ意味は無い
        if (slots_.empty()) {
            return;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i) {
            if (!slots_[i].inUse) {
                continue;
            }

            // フェードは完了時に自分でスロットを解放することがある
            AdvanceFade(i, deltaTime);
            if (!slots_[i].inUse) {
                continue;
            }

            // 鳴り終わったスロットを回収する。これが無いとスロットが増え続ける
            if (slots_[i].voice->HasFinished()) {
                ReleaseSlot(i);
            }
        }
    }

    // ──────────────────────────────────────────────────────────
    // 読み込み
    // ──────────────────────────────────────────────────────────

    SoundClip AudioSystem::LoadClip(const std::string& filename)
    {
        // MP3 のデコードに Media Foundation が要るので初期化完了を待つ
        if (!EnsureInitialized()) {
            return {};
        }

        const std::filesystem::path resolved = AudioPathResolver::Resolve(filename);

        // 同じファイルは PCM を共有する（読み込むたびにデコードして二重に載せない）
        if (auto it = clipIdByPath_.find(resolved.native()); it != clipIdByPath_.end()) {
            return SoundClip(it->second);
        }

        auto data = DecodeFile(resolved);
        if (!data) {
            return {};
        }

        const uint32_t id = nextClipId_++;
        clipIdByPath_.emplace(resolved.native(), id);
        clips_.emplace(id, ClipEntry{ std::move(data), resolved });
        return SoundClip(id);
    }

    void AudioSystem::UnloadUnusedClips()
    {
        // 再生中のクリップを集める。鳴っているボイスは PCM を直接指しているので、
        // 先に剥がすと解放済み領域を読む
        std::unordered_set<uint32_t> usedClips;
        for (const auto& slot : slots_) {
            if (slot.inUse) {
                usedClips.insert(slot.clipId);
            }
        }

        for (auto it = clips_.begin(); it != clips_.end(); ) {
            if (usedClips.contains(it->first)) {
                ++it;
                continue;
            }
            clipIdByPath_.erase(it->second.path.native());
            it = clips_.erase(it);
        }
    }

    std::unique_ptr<AudioData> AudioSystem::DecodeFile(const std::filesystem::path& path) const
    {
        const std::string extension = AudioPathResolver::GetLowerExtension(path);

        for (const auto& decoder : decoders_) {
            if (decoder->CanDecode(extension)) {
                return decoder->Decode(path);
            }
        }

        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}",
            std::format("Unsupported audio format [{}]: {}",
                extension, Logger::GetInstance().PathToUtf8(path)));
        return nullptr;
    }

    // ──────────────────────────────────────────────────────────
    // 再生
    // ──────────────────────────────────────────────────────────

    SoundInstance AudioSystem::Play(const SoundClip& clip, const PlayParams& params)
    {
        // ソースボイス生成に XAudio2 が要る
        if (!EnsureInitialized() || !clip.IsValid()) {
            return {};
        }

        auto clipIt = clips_.find(clip.id_);
        if (clipIt == clips_.end()) {
            // UnloadUnusedClips() で解放された後のハンドル。無音のまま返す
            return {};
        }

        const float targetVolume = std::clamp(params.volume, 0.0f, 1.0f);
        const bool fadesIn = params.fadeInTime > 0.0f;

        auto voice = std::make_unique<SoundVoice>();
        voice->SetPitch(params.pitch);
        voice->SetVolume(fadesIn ? 0.0f : targetVolume);
        if (!voice->Initialize(xAudio2_.Get(), *clipIt->second.data, BusOf(params.bus).submix)) {
            return {};
        }
        voice->Play(params.loop);

        const uint32_t index = AcquireSlot();
        VoiceSlot& slot = slots_[index];
        slot.voice = std::move(voice);
        slot.clipId = clip.id_;
        slot.inUse = true;

        if (fadesIn) {
            StartFade(slot, targetVolume, params.fadeInTime, false);
        }

        return SoundInstance(this, index, slot.generation);
    }

    SoundInstance AudioSystem::Play(const std::string& filename, const PlayParams& params)
    {
        return Play(LoadClip(filename), params);
    }

    void AudioSystem::PlayOneShot(const SoundClip& clip, const PlayParams& params)
    {
        Play(clip, params);
    }

    void AudioSystem::PlayOneShot(const std::string& filename, const PlayParams& params)
    {
        Play(LoadClip(filename), params);
    }

    ScopedSound AudioSystem::PlayScoped(const SoundClip& clip, const PlayParams& params)
    {
        return ScopedSound(Play(clip, params), lifetimeToken_);
    }

    ScopedSound AudioSystem::PlayScoped(const std::string& filename, const PlayParams& params)
    {
        return PlayScoped(LoadClip(filename), params);
    }

    void AudioSystem::StopAll()
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slots_.size()); ++i) {
            if (slots_[i].inUse) {
                slots_[i].voice->Stop();
                ReleaseSlot(i);
            }
        }
    }

    size_t AudioSystem::GetPlayingCount() const
    {
        return static_cast<size_t>(
            std::count_if(slots_.begin(), slots_.end(), [](const VoiceSlot& s) { return s.inUse; }));
    }

    // ──────────────────────────────────────────────────────────
    // 音量（マスター / バス）
    //
    // どれも初期化を待たない。初期化前に呼ばれたら値を覚えるだけにして、
    // 実際の反映は EnsureInitialized() の合流時に行う。ここで待つと
    // BeginInitializeAsync が隠している 0.676 秒が起動シーケンス上に戻ってくる。
    // ──────────────────────────────────────────────────────────

    void AudioSystem::SetMasterVolume(float volume)
    {
        masterVolume_ = std::clamp(volume, 0.0f, 1.0f);

        std::lock_guard<std::mutex> lock(initMutex_);
        if (initCompleted_ && initSucceeded_ && masteringVoice_) {
            masteringVoice_->SetVolume(masterVolume_);
        }
    }

    void AudioSystem::SetBusVolume(AudioBus bus, float volume)
    {
        const float clamped = std::clamp(volume, 0.0f, 1.0f);

        std::lock_guard<std::mutex> lock(initMutex_);
        Bus& target = BusOf(bus);
        if (target.volume == clamped) {
            return;
        }
        target.volume = clamped;
        ApplyBusGainLocked(target);
    }

    float AudioSystem::GetBusVolume(AudioBus bus) const
    {
        return BusOf(bus).volume;
    }

    void AudioSystem::SetBusDuck(AudioBus bus, float duck)
    {
        const float clamped = std::clamp(duck, 0.0f, 1.0f);

        std::lock_guard<std::mutex> lock(initMutex_);
        Bus& target = BusOf(bus);
        // シーン遷移が毎フレーム同じ値を投げてくるので、変化が無ければ何もしない
        if (target.duck == clamped) {
            return;
        }
        target.duck = clamped;
        ApplyBusGainLocked(target);
    }

    float AudioSystem::GetBusDuck(AudioBus bus) const
    {
        return BusOf(bus).duck;
    }

    void AudioSystem::ApplyBusGainLocked(Bus& bus)
    {
        // submix は初期化スレッドが書くので、合流済みが確定してからしか読まない
        if (initCompleted_ && initSucceeded_ && bus.submix) {
            bus.submix->SetVolume(bus.volume * bus.duck);
        }
    }

    AudioSystem::Bus& AudioSystem::BusOf(AudioBus bus)
    {
        return const_cast<Bus&>(std::as_const(*this).BusOf(bus));
    }

    const AudioSystem::Bus& AudioSystem::BusOf(AudioBus bus) const
    {
        const size_t index = static_cast<size_t>(bus);
        // 番兵（Count）や範囲外が渡されたら SE 扱いにする
        return buses_[index < kAudioBusCount ? index : static_cast<size_t>(AudioBus::SE)];
    }

    // ──────────────────────────────────────────────────────────
    // スロット管理
    // ──────────────────────────────────────────────────────────

    uint32_t AudioSystem::AcquireSlot()
    {
        if (!freeSlots_.empty()) {
            const uint32_t index = freeSlots_.back();
            freeSlots_.pop_back();
            return index;
        }

        slots_.emplace_back();
        return static_cast<uint32_t>(slots_.size() - 1);
    }

    void AudioSystem::ReleaseSlot(uint32_t index)
    {
        VoiceSlot& slot = slots_[index];
        if (!slot.inUse) {
            return;
        }

        slot.voice.reset(); // DestroyVoice。PCM より先に必ずボイスを壊す
        slot.inUse = false;
        slot.clipId = 0;
        slot.fading = false;

        // 世代を進める。これで発行済みのハンドルが全て無効になる（0 は無効値なので飛ばす）
        if (++slot.generation == 0) {
            slot.generation = 1;
        }

        freeSlots_.push_back(index);
    }

    AudioSystem::VoiceSlot* AudioSystem::ResolveSlot(uint32_t index, uint32_t generation)
    {
        return const_cast<VoiceSlot*>(std::as_const(*this).ResolveSlot(index, generation));
    }

    const AudioSystem::VoiceSlot* AudioSystem::ResolveSlot(uint32_t index, uint32_t generation) const
    {
        if (generation == 0 || index >= slots_.size()) {
            return nullptr;
        }

        const VoiceSlot& slot = slots_[index];
        // 世代が違う＝このハンドルが指していた再生はもう終わっている
        if (!slot.inUse || slot.generation != generation) {
            return nullptr;
        }
        return &slot;
    }

    // ──────────────────────────────────────────────────────────
    // フェード
    // ──────────────────────────────────────────────────────────

    void AudioSystem::StartFade(VoiceSlot& slot, float targetVolume, float duration, bool stopAfterFade)
    {
        const float target = std::clamp(targetVolume, 0.0f, 1.0f);

        // 時間 0 のフェードは即時反映として扱う（0 除算を避ける）
        if (duration <= 0.0f) {
            slot.fading = false;
            slot.voice->SetVolume(target);
            if (stopAfterFade) {
                slot.voice->Stop();
            }
            return;
        }

        slot.fading = true;
        slot.fadeTimer = 0.0f;
        slot.fadeDuration = duration;
        slot.fadeStartVolume = slot.voice->GetVolume();
        slot.fadeTargetVolume = target;
        slot.stopAfterFade = stopAfterFade;
    }

    void AudioSystem::AdvanceFade(uint32_t index, float deltaTime)
    {
        VoiceSlot& slot = slots_[index];
        if (!slot.fading) {
            return;
        }

        slot.fadeTimer += deltaTime;

        if (slot.fadeTimer >= slot.fadeDuration) {
            slot.fading = false;
            slot.voice->SetVolume(slot.fadeTargetVolume);

            if (slot.stopAfterFade) {
                slot.voice->Stop();
                ReleaseSlot(index);
            }
            return;
        }

        const float t = slot.fadeTimer / slot.fadeDuration;
        slot.voice->SetVolume(slot.fadeStartVolume + (slot.fadeTargetVolume - slot.fadeStartVolume) * t);
    }
}

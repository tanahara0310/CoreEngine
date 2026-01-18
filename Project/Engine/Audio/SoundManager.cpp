#include "SoundManager.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <shlwapi.h>
#include <format>
#include "Engine/Utility/Logger/Logger.h"
#include "Engine/Utility/FileErrorDialog/FileErrorDialog.h"

#pragma comment(lib, "shlwapi.lib")

// 警告を無効化
#pragma warning(push)
#pragma warning(disable : 4267) // size_t から int への変換
#pragma warning(disable : 4244) // double から float への変換

// ===== SoundVoice クラス実装 =====

namespace CoreEngine
{
SoundVoice::SoundVoice()
    : sourceVoice_(nullptr)
    , isPlaying_(false)
    , isPaused_(false)
    , volume_(1.0f)
{
    ZeroMemory(&buffer_, sizeof(XAUDIO2_BUFFER));
}

SoundVoice::~SoundVoice()
{
    CleanupVoice();
}

bool SoundVoice::Initialize(IXAudio2* xAudio2, const SoundData& soundData)
{
    if (!xAudio2 || !soundData.pBuffer) {
        return false;
    }

    // 既存のボイスをクリーンアップ
    CleanupVoice();

    // 新しいSourceVoiceを作成
    HRESULT result = xAudio2->CreateSourceVoice(&sourceVoice_, &soundData.wfex);
    if (FAILED(result)) {
        return false;
    }

    // バッファ設定
    buffer_.pAudioData = soundData.GetBuffer();
    buffer_.AudioBytes = soundData.bufferSize;
    buffer_.Flags = XAUDIO2_END_OF_STREAM;

    return true;
}

void SoundVoice::Play(bool loop)
{
    if (!sourceVoice_) {
        return;
    }

    // 既存のバッファをクリア（再生中の場合も含む）
    sourceVoice_->Stop();
    sourceVoice_->FlushSourceBuffers();

    // ループ設定
    if (loop) {
        buffer_.LoopCount = XAUDIO2_LOOP_INFINITE;
    } else {
        buffer_.LoopCount = 0;
    }

    // バッファをsubmitして再生開始
    HRESULT result = sourceVoice_->SubmitSourceBuffer(&buffer_);
    if (SUCCEEDED(result)) {
        result = sourceVoice_->Start();
        if (SUCCEEDED(result)) {
            isPlaying_ = true;
            isPaused_ = false;
        }
    }
}

void SoundVoice::Stop()
{
    if (!sourceVoice_) {
        return;
    }

    sourceVoice_->Stop();
    sourceVoice_->FlushSourceBuffers();
    isPlaying_ = false;
    isPaused_ = false;
}

void SoundVoice::Pause()
{
    if (!sourceVoice_ || !isPlaying_) {
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
    if (!sourceVoice_) {
        return;
    }

    volume_ = std::clamp(volume, 0.0f, 1.0f);
    sourceVoice_->SetVolume(volume_);
}

float SoundVoice::GetVolume() const
{
    return volume_;
}

bool SoundVoice::IsPlaying() const
{
    if (!sourceVoice_) {
        return false;
    }

    XAUDIO2_VOICE_STATE state;
    sourceVoice_->GetState(&state);
    return (state.BuffersQueued > 0) && !isPaused_;
}

bool SoundVoice::IsPaused() const
{
    return isPaused_;
}

void SoundVoice::CleanupVoice()
{
    if (sourceVoice_) {
        sourceVoice_->Stop();
        sourceVoice_->FlushSourceBuffers();
        sourceVoice_->DestroyVoice();
        sourceVoice_ = nullptr;
    }
    isPlaying_ = false;
    isPaused_ = false;
}

// ===== SoundManager クラス実装 =====
SoundManager::SoundManager()
    : masteringVoice_(nullptr)
    , mfInitialized_(false)
    , nextHandle_(1)
    , masterVolume_(1.0f)
{
}

SoundManager::~SoundManager()
{
    Shutdown();
}

bool SoundManager::Initialize()
{
    HRESULT result = S_OK;

    // XAudio2の初期化
    result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(result)) {
        return false;
    }

    result = xAudio2_->CreateMasteringVoice(&masteringVoice_);
    if (FAILED(result)) {
        return false;
    }

    // Media Foundationの初期化
    if (!InitializeMediaFoundation()) {
        return false;
    }

    return true;
}

bool SoundManager::InitializeMediaFoundation()
{
    HRESULT result = MFStartup(MF_VERSION);
    if (FAILED(result)) {
        return false;
    }

    mfInitialized_ = true;
    return true;
}

void SoundManager::ShutdownMediaFoundation()
{
    if (mfInitialized_) {
        MFShutdown();
        mfInitialized_ = false;
    }
}

SoundHandle SoundManager::LoadSound(const std::string& filename)
{
    // パスを解決
    std::string resolvedPath = ResolveFilePath(filename);

    std::string extension = GetFileExtension(resolvedPath);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    auto soundData = std::make_unique<SoundData>();
    bool loadSuccess = false;

    if (extension == ".wav") {
        loadSuccess = LoadWaveFileInternal(resolvedPath, *soundData);
        soundData->format = "wav";
    } else if (extension == ".mp3") {
        loadSuccess = ExtractPCMDataFromFile(resolvedPath, *soundData);
        soundData->format = "mp3";
    } else {
        // 未対応のフォーマット
        return 0;
    }

    if (!loadSuccess) {
        return 0;
    }

    SoundHandle handle = GenerateHandle();
    soundDataMap_[handle] = std::move(soundData);

    return handle;
}

bool SoundManager::LoadWaveFileInternal(const std::string& filename, SoundData& outSoundData)
{
    Logger::GetInstance().Log(std::format("Loading audio file (WAV): {}", filename), LogLevel::INFO, LogCategory::Audio);

    // ファイル入力ストリームのインスタンス
    std::ifstream file;
    // .wavファイルをバイナリモードで開く
    file.open(filename, std::ios_base::binary);
    // ファイルが開けなかったらエラー
    if (!file.is_open()) {
        std::string errorMsg = std::format("Failed to open audio file: {}\nPlease check if the file exists and the path is correct.", filename);
        Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Audio);
        FileErrorDialog::ShowAudioError("Failed to open WAV file", filename);
        return false;
    }

    // RIFFヘッダの読み込み
    RiffHeader riff;
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
    // RIFFヘッダのチェック
    if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
        std::string errorMsg = std::format("Invalid RIFF header in audio file: {}", filename);
        Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Audio);
        return false;
    }
    // タイプがWAVEかチェック
    if (strncmp(riff.type, "WAVE", 4) != 0) {
        std::string errorMsg = std::format("Invalid WAVE type in audio file: {}", filename);
        Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Audio);
        return false;
    }

    // FormatChunkの読み込み
    FormatChunk format = {};
    // チャンクヘッダーの確認
    file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
    if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
        std::string errorMsg = std::format("Invalid format chunk in audio file: {}", filename);
        Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Audio);
        return false;
    }

    // チャンク本体の読み込み
    if (static_cast<size_t>(format.chunk.size) > sizeof(format.fmt)) {
        return false;
    }
    file.read(reinterpret_cast<char*>(&format.fmt), std::min<size_t>(static_cast<size_t>(format.chunk.size), sizeof(WAVEFORMATEX)));

    // DateChunkの読み込み
    ChunkHeader data;
    file.read(reinterpret_cast<char*>(&data), sizeof(data));
    // JUNKチャンクを検出した場合
    if (strncmp(data.id, "JUNK", 4) == 0) {
        // 読み取り位置をJUNKチャンクの終わりまで進める
        file.seekg(static_cast<std::streamoff>(data.size), std::ios_base::cur);
        // 再読み込み
        file.read(reinterpret_cast<char*>(&data), sizeof(data));
    }

    if (strncmp(data.id, "data", 4) != 0) {
        return false;
    }

    // Dataチャンクのデータ部の読み込み（スマートポインタで管理）
    auto pBuffer = std::make_unique<BYTE[]>(static_cast<size_t>(data.size));
    file.read(reinterpret_cast<char*>(pBuffer.get()), static_cast<std::streamsize>(data.size));
    // waveファイルを閉じる
    file.close();

    // 結果を設定
    outSoundData.wfex = format.fmt;
    outSoundData.pBuffer = std::move(pBuffer);
    outSoundData.bufferSize = static_cast<unsigned int>(data.size);
    outSoundData.format = "wav";

    return true;
}

bool SoundManager::ExtractPCMDataFromFile(const std::string& filename, SoundData& outSoundData)
{
    Logger::GetInstance().Log(std::format("Loading audio file (MP3/compressed): {}", filename), LogLevel::INFO, LogCategory::Audio);

    if (!mfInitialized_) {
        Logger::GetInstance().Log("Media Foundation not initialized", LogLevel::Error, LogCategory::Audio);
        return false;
    }

    std::wstring wFilename = Logger::GetInstance().ConvertString(filename);

    // SourceReaderを作成
    Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
    HRESULT hr = MFCreateSourceReaderFromURL(wFilename.c_str(), nullptr, &sourceReader);
    if (FAILED(hr)) {
        std::string errorMsg = std::format("Failed to create source reader for audio file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and is a valid audio file.", 
            filename, static_cast<unsigned int>(hr));
        Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Audio);
        FileErrorDialog::ShowAudioError("Failed to open MP3/compressed audio file", filename, hr);
        return false;
    }

    // PCMフォーマットを設定
    Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
    hr = MFCreateMediaType(&pcmType);
    if (FAILED(hr)) {
        return false;
    }

    hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (FAILED(hr)) {
        return false;
    }

    hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (FAILED(hr)) {
        return false;
    }

    // 出力メディアタイプを設定
    hr = sourceReader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get());
    if (FAILED(hr)) {
        return false;
    }

    // 実際の出力フォーマットを取得
    Microsoft::WRL::ComPtr<IMFMediaType> actualType;
    hr = sourceReader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actualType);
    if (FAILED(hr)) {
        return false;
    }

    // WAVEFORMATEXを作成
    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(actualType.Get(), &waveFormat, &waveFormatSize);
    if (FAILED(hr)) {
        return false;
    }

    // PCMデータを読み込み
    std::vector<BYTE> audioData;
    Microsoft::WRL::ComPtr<IMFSample> sample;
    DWORD flags = 0;

    while (true) {
        hr = sourceReader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) {
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }

        if (sample) {
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (SUCCEEDED(hr)) {
                BYTE* audioBytes = nullptr;
                DWORD audioLength = 0;
                hr = buffer->Lock(&audioBytes, nullptr, &audioLength);
                if (SUCCEEDED(hr)) {
                    audioData.insert(audioData.end(), audioBytes, audioBytes + audioLength);
                    buffer->Unlock();
                }
            }
        }

        sample.Reset();
    }

    // データをコピー（スマートポインタで管理）
    if (!audioData.empty()) {
        outSoundData.wfex = *waveFormat;
        outSoundData.bufferSize = static_cast<unsigned int>(audioData.size());
        outSoundData.pBuffer = std::make_unique<BYTE[]>(outSoundData.bufferSize);
        memcpy(outSoundData.pBuffer.get(), audioData.data(), outSoundData.bufferSize);
        outSoundData.format = "mp3";
    }

    // メモリを解放
    CoTaskMemFree(waveFormat);

    return !audioData.empty();
}

void SoundManager::UnloadSound(SoundHandle handle)
{
    auto dataIt = soundDataMap_.find(handle);
    if (dataIt != soundDataMap_.end()) {
        soundDataMap_.erase(dataIt);
    }

    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        soundVoiceMap_.erase(voiceIt);
    }

    pendingVolume_.erase(handle);
}

bool SoundManager::PlaySound(SoundHandle handle, bool loop)
{
    auto dataIt = soundDataMap_.find(handle);
    if (dataIt == soundDataMap_.end()) {
        return false;
    }

    // 既存のボイスがある場合
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        // 予約音量があれば既存ボイスにも適用しておく
        if (auto it = pendingVolume_.find(handle); it != pendingVolume_.end()) {
            voiceIt->second->SetVolume(it->second);
        }

        // 再生（Play内部でStop/Flushが実行される）
        voiceIt->second->Play(loop);
        return true;
    }

    // 新しいボイスを作成する経路
    auto voice = std::make_unique<SoundVoice>();
    if (!voice->Initialize(xAudio2_.Get(), *dataIt->second)) {
        return false;
    }

    // 予約音量を適用
    if (auto it = pendingVolume_.find(handle); it != pendingVolume_.end()) {
        voice->SetVolume(it->second);
    }

    voice->Play(loop);
    soundVoiceMap_[handle] = std::move(voice);
    return true;
}

void SoundManager::StopSound(SoundHandle handle)
{
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        voiceIt->second->Stop();
    }
}

void SoundManager::StopAllSounds()
{
    for (auto& voice : soundVoiceMap_) {
        voice.second->Stop();
    }
}

void SoundManager::PauseSound(SoundHandle handle)
{
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        voiceIt->second->Pause();
    }
}

void SoundManager::ResumeSound(SoundHandle handle)
{
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        voiceIt->second->Resume();
    }
}

void SoundManager::SetVolume(SoundHandle handle, float volume)
{
    float v = std::clamp(volume, 0.0f, 1.0f);
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        voiceIt->second->SetVolume(v);
    } else {
        // ボイス未作成時：予約しておき、初回Playの直前に適用
        pendingVolume_[handle] = v;
    }
}

float SoundManager::GetVolume(SoundHandle handle) const
{
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        return voiceIt->second->GetVolume();
    }
    return 0.0f;
}

void SoundManager::SetMasterVolume(float volume)
{
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (masteringVoice_) {
        masteringVoice_->SetVolume(masterVolume_);
    }
}

float SoundManager::GetMasterVolume() const
{
    return masterVolume_;
}

bool SoundManager::IsPlaying(SoundHandle handle) const
{
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        return voiceIt->second->IsPlaying();
    }
    return false;
}

bool SoundManager::IsPaused(SoundHandle handle) const
{
    auto voiceIt = soundVoiceMap_.find(handle);
    if (voiceIt != soundVoiceMap_.end()) {
        return voiceIt->second->IsPaused();
    }
    return false;
}

std::string SoundManager::GetFileExtension(const std::string& filename) const
{
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        return filename.substr(dotPos);
    }
    return "";
}

SoundHandle SoundManager::GenerateHandle()
{
    return nextHandle_++;
}

void SoundManager::Shutdown()
{
    // 全てのサウンドを停止
    StopAllSounds();

    // ボイスとデータをクリア
    soundVoiceMap_.clear();
    soundDataMap_.clear();

    // XAudio2の解放
    if (masteringVoice_) {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }
    xAudio2_.Reset();

    // Media Foundationの終了
    ShutdownMediaFoundation();
}

void SoundManager::StopAndUnload(SoundHandle handle)
{
    if (handle != 0) {
        StopSound(handle);
        UnloadSound(handle);
    }
    pendingVolume_.erase(handle);
}

// ===== SoundResourceクラスの実装 =====
SoundManager::SoundResource::SoundResource(SoundManager* manager, SoundHandle handle)
    : manager_(manager)
    , handle_(handle)
    , isFading_(false)
    , isFadingIn_(false)
    , fadeTimer_(0.0f)
    , fadeDuration_(0.0f)
    , fadeStartVolume_(0.0f)
    , fadeTargetVolume_(1.0f)
    , stopAfterFade_(false)
{
}

SoundManager::SoundResource::~SoundResource()
{
    if (manager_ && handle_ != 0) {
        manager_->StopAndUnload(handle_);
    }
}

SoundManager::SoundResource::SoundResource(SoundResource&& other) noexcept
    : manager_(other.manager_)
    , handle_(other.handle_)
    , isFading_(other.isFading_)
    , isFadingIn_(other.isFadingIn_)
    , fadeTimer_(other.fadeTimer_)
    , fadeDuration_(other.fadeDuration_)
    , fadeStartVolume_(other.fadeStartVolume_)
    , fadeTargetVolume_(other.fadeTargetVolume_)
    , stopAfterFade_(other.stopAfterFade_)
{
    other.manager_ = nullptr;
    other.handle_ = 0;
}

SoundManager::SoundResource& SoundManager::SoundResource::operator=(SoundResource&& other) noexcept
{
    if (this != &other) {
        // 既存のリソースを解放
        if (manager_ && handle_ != 0) {
            manager_->StopAndUnload(handle_);
        }

        // 新しいリソースを取得
        manager_ = other.manager_;
        handle_ = other.handle_;
        isFading_ = other.isFading_;
        isFadingIn_ = other.isFadingIn_;
        fadeTimer_ = other.fadeTimer_;
        fadeDuration_ = other.fadeDuration_;
        fadeStartVolume_ = other.fadeStartVolume_;
        fadeTargetVolume_ = other.fadeTargetVolume_;
        stopAfterFade_ = other.stopAfterFade_;

        // 移動元をクリア
        other.manager_ = nullptr;
        other.handle_ = 0;
    }
    return *this;
}

bool SoundManager::SoundResource::Play(bool loop)
{
    if (!IsValid()) {
        return false;
    }
    return manager_->PlaySound(handle_, loop);
}

void SoundManager::SoundResource::Stop()
{
    if (IsValid()) {
        manager_->StopSound(handle_);
    }
}

void SoundManager::SoundResource::Pause()
{
    if (IsValid()) {
        manager_->PauseSound(handle_);
    }
}

void SoundManager::SoundResource::Resume()
{
    if (IsValid()) {
        manager_->ResumeSound(handle_);
    }
}

void SoundManager::SoundResource::SetVolume(float volume)
{
    if (IsValid()) {
        manager_->SetVolume(handle_, volume);
    }
}

float SoundManager::SoundResource::GetVolume() const
{
    if (IsValid()) {
        return manager_->GetVolume(handle_);
    }
    return 0.0f;
}

bool SoundManager::SoundResource::IsPlaying() const
{
    if (IsValid()) {
        return manager_->IsPlaying(handle_);
    }
    return false;
}

bool SoundManager::SoundResource::IsPaused() const
{
    if (IsValid()) {
        return manager_->IsPaused(handle_);
    }
    return false;
}

std::unique_ptr<SoundManager::SoundResource> SoundManager::CreateSoundResource(const std::string& filename)
{
    SoundHandle handle = LoadSound(filename);
    if (handle != 0) {
        return std::make_unique<SoundResource>(this, handle);
    }
    return nullptr;
}

void SoundManager::SoundResource::FadeIn(float duration, float targetVolume)
{
    if (!IsValid()) {
        return;
    }
    
    isFading_ = true;
    isFadingIn_ = true;
    fadeTimer_ = 0.0f;
    fadeDuration_ = duration;
    fadeStartVolume_ = 0.0f;
    fadeTargetVolume_ = std::clamp(targetVolume, 0.0f, 1.0f);
    stopAfterFade_ = false;
    
    // 初期音量を0に設定
    manager_->SetVolume(handle_, 0.0f);
}

void SoundManager::SoundResource::FadeOut(float duration, bool stopAfterFade)
{
    if (!IsValid()) {
        return;
    }
    
    isFading_ = true;
    isFadingIn_ = false;
    fadeTimer_ = 0.0f;
    fadeDuration_ = duration;
    fadeStartVolume_ = manager_->GetVolume(handle_);
    fadeTargetVolume_ = 0.0f;
    stopAfterFade_ = stopAfterFade;
}

void SoundManager::SoundResource::UpdateFade(float deltaTime)
{
    if (!isFading_ || !IsValid()) {
        return;
    }
    
    fadeTimer_ += deltaTime;
    
    // フェード完了チェック
    if (fadeTimer_ >= fadeDuration_) {
        fadeTimer_ = fadeDuration_;
        isFading_ = false;
        
        // 目標音量に設定
        manager_->SetVolume(handle_, fadeTargetVolume_);
        
        // フェードアウト後に停止する場合
        if (!isFadingIn_ && stopAfterFade_) {
            manager_->StopSound(handle_);
        }
        return;
    }
    
    // 音量を補間
    float t = fadeTimer_ / fadeDuration_;
    float currentVolume = fadeStartVolume_ + (fadeTargetVolume_ - fadeStartVolume_) * t;
    manager_->SetVolume(handle_, currentVolume);
}

std::string SoundManager::ResolveFilePath(const std::string& filePath) const
{
    // すでにAssetsで始まっている場合はそのまま返す
    if (filePath.starts_with("Assets/") || filePath.starts_with("Assets\\")) {
        return filePath;
    }

    // 絶対パス（C:/ など）の場合はそのまま返す
    if (filePath.length() >= 2 && filePath[1] == ':') {
        return filePath;
    }

    // それ以外の場合はbasePath_を前に追加
    return basePath_ + filePath;
}

#pragma warning(pop) // 警告設定を復元
}

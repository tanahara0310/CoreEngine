#include "pch.h"
#include "MediaFoundationDecoder.h"

#include <cstring>
#include <format>
#include <vector>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "Audio/Decoder/AudioData.h"
#include "Utility/FileErrorDialog/FileErrorDialog.h"
#include "Utility/Logger/Logger.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace CoreEngine
{
    namespace
    {
        void LogError(const std::filesystem::path& path, const std::string& reason, HRESULT hr)
        {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}",
                std::format("MP3 decode failed ({}): {} / HRESULT: 0x{:08X}",
                    reason, Logger::GetInstance().PathToUtf8(path), static_cast<unsigned int>(hr)));
        }

        /// @brief SourceReader の出力を PCM に固定する
        Microsoft::WRL::ComPtr<IMFMediaType> ConfigurePcmOutput(IMFSourceReader* reader, HRESULT& hr)
        {
            Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
            hr = MFCreateMediaType(&pcmType);
            if (FAILED(hr)) { return nullptr; }

            hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (FAILED(hr)) { return nullptr; }

            hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (FAILED(hr)) { return nullptr; }

            hr = reader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get());
            if (FAILED(hr)) { return nullptr; }

            // 実際に確定した出力フォーマットを取り直す（要求どおりとは限らない）
            Microsoft::WRL::ComPtr<IMFMediaType> actualType;
            hr = reader->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actualType);
            if (FAILED(hr)) { return nullptr; }

            return actualType;
        }

        /// @brief ストリーム終端まで読み切って PCM を連結する
        std::vector<uint8_t> ReadAllSamples(IMFSourceReader* reader)
        {
            std::vector<uint8_t> pcm;
            Microsoft::WRL::ComPtr<IMFSample> sample;
            DWORD flags = 0;

            while (true) {
                HRESULT hr = reader->ReadSample(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                    0, nullptr, &flags, nullptr, &sample);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                    break;
                }

                if (sample) {
                    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
                    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                        BYTE* bytes = nullptr;
                        DWORD length = 0;
                        if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &length))) {
                            pcm.insert(pcm.end(), bytes, bytes + length);
                            buffer->Unlock();
                        }
                    }
                }
                sample.Reset();
            }
            return pcm;
        }
    }

    std::unique_ptr<AudioData> MediaFoundationDecoder::Decode(const std::filesystem::path& path) const
    {
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Audio, "{}",
            std::format("Loading audio file (MP3/compressed): {}", Logger::GetInstance().PathToUtf8(path)));

        // Media Foundation はワイド API。path.c_str() がそのまま渡せる
        Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
        HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &sourceReader);
        if (FAILED(hr)) {
            const std::string utf8Path = Logger::GetInstance().PathToUtf8(path);
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}",
                std::format("Failed to create source reader for audio file: {}\nHRESULT: 0x{:08X}\n"
                            "Please check if the file exists and is a valid audio file.",
                    utf8Path, static_cast<unsigned int>(hr)));
            FileErrorDialog::ShowAudioError("Failed to open MP3/compressed audio file", utf8Path, hr);
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> actualType = ConfigurePcmOutput(sourceReader.Get(), hr);
        if (!actualType) {
            LogError(path, "failed to configure PCM output", hr);
            return nullptr;
        }

        // MFCreateWaveFormatExFromMFMediaType は CoTaskMemAlloc したバッファを返す。
        // 拡張部（cbSize 分）を含む可能性があるので waveFormatSize バイトを丸ごと持つ
        WAVEFORMATEX* waveFormat = nullptr;
        UINT32 waveFormatSize = 0;
        hr = MFCreateWaveFormatExFromMFMediaType(actualType.Get(), &waveFormat, &waveFormatSize);
        if (FAILED(hr)) {
            LogError(path, "failed to build WAVEFORMATEX", hr);
            return nullptr;
        }

        auto result = std::make_unique<AudioData>();
        result->SetFormat(waveFormat, waveFormatSize);
        CoTaskMemFree(waveFormat);

        const std::vector<uint8_t> pcm = ReadAllSamples(sourceReader.Get());
        if (pcm.empty()) {
            LogError(path, "no audio samples decoded", S_OK);
            return nullptr;
        }

        result->pcmSize = static_cast<uint32_t>(pcm.size());
        result->pcm = std::make_unique<uint8_t[]>(result->pcmSize);
        std::memcpy(result->pcm.get(), pcm.data(), result->pcmSize);

        return result;
    }
}

#include "pch.h"
#include "WavDecoder.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

#include "Audio/Decoder/AudioData.h"
#include "Utility/FileErrorDialog/FileErrorDialog.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief RIFF チャンクのヘッダ
        struct ChunkHeader {
            char id[4];      ///< チャンク ID
            uint32_t size;   ///< チャンク本体のバイト数
        };

        /// @brief RIFF ヘッダ（"RIFF" + サイズ + "WAVE"）
        struct RiffHeader {
            ChunkHeader chunk;
            char type[4];
        };

        bool IdIs(const char (&id)[4], const char* expected)
        {
            return std::strncmp(id, expected, 4) == 0;
        }

        void LogError(const std::filesystem::path& path, const std::string& reason)
        {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}",
                std::format("WAV decode failed ({}): {}", reason, Logger::GetInstance().PathToUtf8(path)));
        }
    }

    std::unique_ptr<AudioData> WavDecoder::Decode(const std::filesystem::path& path) const
    {
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Audio, "{}",
            std::format("Loading audio file (WAV): {}", Logger::GetInstance().PathToUtf8(path)));

        std::ifstream file(path, std::ios_base::binary);
        if (!file.is_open()) {
            const std::string utf8Path = Logger::GetInstance().PathToUtf8(path);
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Audio, "{}",
                std::format("Failed to open audio file: {}\nPlease check if the file exists and the path is correct.", utf8Path));
            FileErrorDialog::ShowAudioError("Failed to open WAV file", utf8Path);
            return nullptr;
        }

        RiffHeader riff{};
        if (!file.read(reinterpret_cast<char*>(&riff), sizeof(riff))) {
            LogError(path, "truncated RIFF header");
            return nullptr;
        }
        if (!IdIs(riff.chunk.id, "RIFF") || !IdIs(riff.type, "WAVE")) {
            LogError(path, "not a RIFF/WAVE file");
            return nullptr;
        }

        auto result = std::make_unique<AudioData>();

        // fmt / data 以外（LIST・JUNK・cue 等）は読み飛ばす。旧実装は JUNK だけを
        // 特別扱いしていたため、LIST を持つ一般的な WAV で読み込みに失敗していた
        while (file) {
            ChunkHeader chunk{};
            if (!file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk))) {
                break; // 正常終端（これ以上チャンクが無い）
            }

            if (IdIs(chunk.id, "fmt ")) {
                if (chunk.size < sizeof(WAVEFORMATEX) - sizeof(WORD)) { // cbSize 抜きの PCMWAVEFORMAT 未満は不正
                    LogError(path, "format chunk too small");
                    return nullptr;
                }

                // WAVE_FORMAT_EXTENSIBLE も丸ごと保持する（切り詰めると XAudio2 が壊れる）
                std::vector<uint8_t> blob(std::max<size_t>(chunk.size, sizeof(WAVEFORMATEX)));
                if (!file.read(reinterpret_cast<char*>(blob.data()), chunk.size)) {
                    LogError(path, "truncated format chunk");
                    return nullptr;
                }
                result->formatBlob = std::move(blob);
            } else if (IdIs(chunk.id, "data")) {
                if (!result->Format()) {
                    LogError(path, "data chunk appeared before format chunk");
                    return nullptr;
                }

                auto pcm = std::make_unique<uint8_t[]>(chunk.size);
                if (!file.read(reinterpret_cast<char*>(pcm.get()), chunk.size)) {
                    LogError(path, "truncated data chunk");
                    return nullptr;
                }
                result->pcm = std::move(pcm);
                result->pcmSize = chunk.size;
                break; // 必要なものは揃った
            } else {
                // 未知のチャンクはスキップ。RIFF はチャンクを 2 バイト境界に揃える
                const std::streamoff skip = static_cast<std::streamoff>(chunk.size) + (chunk.size & 1);
                file.seekg(skip, std::ios_base::cur);
            }
        }

        if (!result->IsValid()) {
            LogError(path, "format or data chunk not found");
            return nullptr;
        }
        return result;
    }
}

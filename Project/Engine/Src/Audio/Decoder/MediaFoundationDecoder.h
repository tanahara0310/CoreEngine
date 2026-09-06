#pragma once

#include "Audio/Decoder/IAudioDecoder.h"

namespace CoreEngine
{
    /// @brief Media Foundation で圧縮音声（MP3 など）を PCM に展開するデコーダ
    /// @warning 使用前に MFStartup() が済んでいること（AudioSystem の初期化が担当する）。
    class MediaFoundationDecoder final : public IAudioDecoder {
    public:
        const char* GetName() const override { return "MediaFoundation"; }

        bool CanDecode(std::string_view extension) const override
        {
            return extension == ".mp3";
        }

        std::unique_ptr<AudioData> Decode(const std::filesystem::path& path) const override;
    };
}

#pragma once

#include "Audio/Decoder/IAudioDecoder.h"

namespace CoreEngine
{
    /// @brief RIFF/WAVE ファイルを読み込むデコーダ
    class WavDecoder final : public IAudioDecoder {
    public:
        const char* GetName() const override { return "WAV"; }
        bool CanDecode(std::string_view extension) const override { return extension == ".wav"; }
        std::unique_ptr<AudioData> Decode(const std::filesystem::path& path) const override;
    };
}

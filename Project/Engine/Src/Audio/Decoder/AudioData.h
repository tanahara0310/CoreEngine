#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <mmreg.h>

namespace CoreEngine
{
    /// @brief デコード済みの音声データ（波形フォーマット + 生 PCM）
    /// @details AudioSystem のクリップキャッシュが所有する。再生中のソースボイスは
    ///          pcm を直接指すので、ボイスより先にこれを壊してはいけない。
    /// @note AudioSystem の内部表現。ゲーム側は SoundClip を使うこと。
    struct AudioData {
        /// @brief 波形フォーマット
        /// @details WAVE_FORMAT_EXTENSIBLE（cbSize > 0）も入るよう可変長で持つ。
        ///          WAVEFORMATEX へ切り詰めると拡張部を失ったまま cbSize だけ残り、
        ///          XAudio2 が範囲外を読む。
        std::vector<uint8_t> formatBlob;

        std::unique_ptr<uint8_t[]> pcm; ///< 生 PCM
        uint32_t pcmSize = 0;           ///< pcm のバイト数

        /// @brief 波形フォーマットを WAVEFORMATEX として見る
        const WAVEFORMATEX* Format() const {
            return formatBlob.size() >= sizeof(WAVEFORMATEX)
                ? reinterpret_cast<const WAVEFORMATEX*>(formatBlob.data())
                : nullptr;
        }

        /// @brief 再生可能なデータが揃っているか
        bool IsValid() const { return Format() != nullptr && pcm != nullptr && pcmSize > 0; }

        /// @brief WAVEFORMATEX（+ 拡張部）をコピーして formatBlob に格納する
        void SetFormat(const WAVEFORMATEX* format, size_t sizeInBytes) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(format);
            formatBlob.assign(bytes, bytes + sizeInBytes);
        }
    };
}

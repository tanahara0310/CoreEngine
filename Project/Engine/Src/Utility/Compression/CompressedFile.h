#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace CoreEngine
{
    /// @brief zstd で圧縮したファイルの読み書き
    /// @details 書き出しはクック済みのデータを zstd のフレーム 1 つに圧縮して置く。
    ///          読む側は先頭の印で見分け、圧縮していないファイルはそのまま読む。
    namespace CompressedFile
    {
        /// @brief 書き出しで使う圧縮の強さ
        inline constexpr int kCompressionLevel = 9;

        /// @brief ファイルを全部読む（zstd で圧縮してあれば展開する）
        /// @return 開けない・壊れているときは false
        bool Read(const std::filesystem::path& path, std::vector<uint8_t>& data);

        /// @brief ファイルの先頭 size バイトだけを読む（zstd で圧縮してあれば、その分だけ展開する）
        /// @return 開けない・壊れているときは false。ファイルが size より短ければ全部を返す
        bool ReadHead(const std::filesystem::path& path, size_t size, std::vector<uint8_t>& data);

        /// @brief source を zstd で圧縮して destination に書く（あれば上書き）
        /// @return 読めない・書けないときは false
        bool WriteCompressed(const std::filesystem::path& source, const std::filesystem::path& destination);
    }
}

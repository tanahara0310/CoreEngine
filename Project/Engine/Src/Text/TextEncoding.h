#pragma once

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief UTF-8 文字列をコードポイント列へ分解する
    /// @details
    ///  日本語を扱う以上、std::string を 1 バイトずつ見る経路は必ず壊れる。
    ///  文字を数える・グリフを引くといった処理は全てここを通してから行う。
    /// @param utf8 UTF-8 文字列（ソース中の u8 リテラル・JSON からの読み込みなど）
    /// @return コードポイント列。不正なバイト列は U+FFFD へ置換する
    inline std::vector<char32_t> Utf8ToUtf32(const std::string& utf8)
    {
        constexpr char32_t kReplacement = 0xFFFD;

        std::vector<char32_t> result;
        result.reserve(utf8.size());

        const auto* p = reinterpret_cast<const unsigned char*>(utf8.data());
        const auto* end = p + utf8.size();

        while (p < end) {
            const unsigned char lead = *p;
            int extraBytes = 0;
            char32_t codePoint = 0;

            if (lead < 0x80) { codePoint = lead;        extraBytes = 0; }
            else if ((lead & 0xE0) == 0xC0) { codePoint = lead & 0x1F; extraBytes = 1; }
            else if ((lead & 0xF0) == 0xE0) { codePoint = lead & 0x0F; extraBytes = 2; }
            else if ((lead & 0xF8) == 0xF0) { codePoint = lead & 0x07; extraBytes = 3; }
            else {
                // 継続バイトが先頭に来た等。1 バイト読み飛ばして復帰する
                result.push_back(kReplacement);
                ++p;
                continue;
            }

            // 末尾で多バイト文字が切れている場合（extraBytes == 0 なら p < end なので常に偽）
            if (p + extraBytes >= end) {
                result.push_back(kReplacement);
                break;
            }

            bool valid = true;
            for (int i = 1; i <= extraBytes; ++i) {
                const unsigned char continuation = p[i];
                if ((continuation & 0xC0) != 0x80) { valid = false; break; }
                codePoint = (codePoint << 6) | (continuation & 0x3F);
            }

            if (!valid) {
                result.push_back(kReplacement);
                ++p;
                continue;
            }

            result.push_back(codePoint);
            p += extraBytes + 1;
        }

        return result;
    }
}

#pragma once

#include <algorithm>
#include <cstdint>

namespace CoreEngine::TextLineBreak
{
    /// @brief 全角として扱う文字か（和文・記号・全角英数）
    /// @details 和文は単語の切れ目が無いので「どこでも折れる」。
    ///          逆にラテン文字は単語の途中で折ってはいけない。
    ///          その判定にこの区別を使う。
    inline bool IsWide(char32_t cp)
    {
        return (cp >= 0x1100 && cp <= 0x115F)   // ハングル字母
            || (cp >= 0x2E80 && cp <= 0x303E)   // CJK 部首・記号（読点・句点を含む）
            || (cp >= 0x3041 && cp <= 0x33FF)   // かな・カナ・互換文字
            || (cp >= 0x3400 && cp <= 0x4DBF)   // CJK 拡張 A
            || (cp >= 0x4E00 && cp <= 0x9FFF)   // CJK 統合漢字
            || (cp >= 0xF900 && cp <= 0xFAFF)   // CJK 互換漢字
            || (cp >= 0xFF00 && cp <= 0xFF60)   // 全角英数・記号
            || (cp >= 0xFFE0 && cp <= 0xFFE6)   // 全角通貨記号
            || (cp >= 0x20000 && cp <= 0x2FA1F);// CJK 拡張 B 以降
    }

    /// @brief 行頭に来てはいけない文字（行頭禁則）
    /// @details 句読点・閉じ括弧・小書き仮名・音引きなど。
    ///          これらが行頭に落ちると日本語として読めなくなる。
    inline bool IsProhibitedLineStart(char32_t cp)
    {
        switch (cp) {
            // 句読点・中点
        case U'、': case U'。': case U'，': case U'．': case U'・':
        case U'：': case U'；': case U'？': case U'！':
        case U',': case U'.': case U':': case U';': case U'?': case U'!':
            // 閉じ括弧
        case U'）': case U'］': case U'｝': case U'」': case U'』':
        case U'】': case U'〉': case U'》': case U'〕': case U'〙': case U'〗':
        case U')': case U']': case U'}':
            // 繰り返し・音引き・区切り
        case U'ー': case U'〜': case U'～': case U'ゝ': case U'ゞ':
        case U'ヽ': case U'ヾ': case U'々': case U'〃':
        case U'…': case U'‥': case U'ヵ': case U'ヶ':
            // 小書き仮名
        case U'ぁ': case U'ぃ': case U'ぅ': case U'ぇ': case U'ぉ':
        case U'っ': case U'ゃ': case U'ゅ': case U'ょ': case U'ゎ':
        case U'ァ': case U'ィ': case U'ゥ': case U'ェ': case U'ォ':
        case U'ッ': case U'ャ': case U'ュ': case U'ョ': case U'ヮ':
            return true;
        default:
            return false;
        }
    }

    /// @brief 行末に来てはいけない文字（行末禁則）
    /// @details 開き括弧の類。これらが行末に残ると次の行と離れて読みにくい。
    inline bool IsProhibitedLineEnd(char32_t cp)
    {
        switch (cp) {
        case U'（': case U'［': case U'｛': case U'「': case U'『':
        case U'【': case U'〈': case U'《': case U'〔': case U'〘': case U'〖':
        case U'(': case U'[': case U'{':
            return true;
        default:
            return false;
        }
    }

    /// @brief `prev` と `next` の間で行を折ってよいか
    /// @param prev 直前の文字
    /// @param next 次の文字
    /// @details
    ///  - 和文はどこでも折れる（ただし禁則に当たる位置は除く）
    ///  - 欧文は空白の後だけ折れる（単語の途中で切らない）
    inline bool CanBreakBetween(char32_t prev, char32_t next)
    {
        if (IsProhibitedLineEnd(prev)) { return false; }
        if (IsProhibitedLineStart(next)) { return false; }

        if (prev == U' ' || prev == U'\t' || prev == U'　') { return true; }
        if (IsWide(prev) || IsWide(next)) { return true; }

        return false; // 欧文の単語途中
    }
}

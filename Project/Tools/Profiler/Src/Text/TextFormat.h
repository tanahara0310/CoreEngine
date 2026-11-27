#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/// @file TextFormat.h
/// @brief 画面に出す数字と文字の形を揃える

namespace CoreEngine::Profiler::Text
{
    /// @brief 回数を 3 桁ごとのカンマ付きにする（2136 → "2,136"）
    std::string FormatCount(uint64_t value);

    /// @brief ナノ秒をミリ秒の文字にする。大きい値ほど小数の桁を減らす（26743400000 → "26,743.4"、31300 → "0.031"）
    std::string FormatMs(double nanoseconds);

    /// @brief ナノ秒を「分:秒」にする（36000000000 → "0:36"）
    std::string FormatClock(int64_t nanoseconds);

    /// @brief パスからファイル名だけを取り出す（区切りは \ と / のどちらでもよい）
    std::string_view FileName(std::string_view path);

    /// @brief UTF-16 を UTF-8 にする
    std::string ToUtf8(std::wstring_view text);

    /// @brief UTF-8 を UTF-16 にする
    std::wstring ToWide(std::string_view text);

    /// @brief 大文字と小文字を区別せず、text に term が含まれるか（ASCII の範囲だけ大文字と小文字を同じに扱う）
    bool ContainsIgnoreCase(std::string_view text, std::string_view term);
}

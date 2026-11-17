#pragma once

#include <Windows.h>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief UTF-8 の文字列を UTF-16 にする（不正な並びは U+FFFD になる）
    /// @return 空か、変換できなければ空
    inline std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty()) {
            return {};
        }
        const int length = MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (length <= 0) {
            return {};
        }
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
            wide.data(), length);
        return wide;
    }

    /// @brief UTF-16 の文字列を UTF-8 にする
    /// @return 空か、変換できなければ空
    inline std::string WideToUtf8(std::wstring_view wide)
    {
        if (wide.empty()) {
            return {};
        }
        const int length = WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) {
            return {};
        }
        std::string utf8(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
            utf8.data(), length, nullptr, nullptr);
        return utf8;
    }
}

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace CoreEngine
{
    /// @brief 文字列を固定長の char 配列へ写す（入り切らない分は落とし、'\0' で終える）
    template <std::size_t N>
    void CopyToBuffer(char (&buffer)[N], std::string_view text) noexcept
    {
        static_assert(N > 0);
        const std::size_t length = (std::min)(text.size(), N - 1);
        std::memcpy(buffer, text.data(), length);
        buffer[length] = '\0';
    }
}

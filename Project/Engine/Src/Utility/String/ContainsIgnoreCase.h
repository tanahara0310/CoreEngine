#pragma once

#include <algorithm>
#include <string_view>

namespace CoreEngine
{
    /// @brief ASCII の英字だけ大文字小文字を無視して、text が query を含むか（query が空なら true）
    inline bool ContainsIgnoreCase(std::string_view text, std::string_view query)
    {
        if (query.empty()) {
            return true;
        }
        const auto lower = [](char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        };
        const auto found = std::search(text.begin(), text.end(), query.begin(), query.end(),
            [&lower](char a, char b) { return lower(a) == lower(b); });
        return found != text.end();
    }
}

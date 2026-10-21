#pragma once

#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief 起動の引数を読む
    namespace CommandLine
    {
        /// @brief 起動の引数に option（`--launcher` など）があるか
        bool HasOption(std::wstring_view option);

        /// @brief option の次の引数（`--workers 1` の `1`）。無ければ空
        std::wstring GetValue(std::wstring_view option);
    }
}

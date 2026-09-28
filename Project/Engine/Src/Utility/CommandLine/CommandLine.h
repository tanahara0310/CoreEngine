#pragma once

#include <string_view>

namespace CoreEngine
{
    /// @brief 起動の引数を読む
    namespace CommandLine
    {
        /// @brief 起動の引数に option（`--launcher` など）があるか
        bool HasOption(std::wstring_view option);
    }
}

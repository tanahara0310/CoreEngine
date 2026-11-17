#pragma once

#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief 失敗の訳を outError へ書いて false を返す（outError が nullptr なら書かない）
    inline bool FailWith(std::string* outError, std::string_view reason)
    {
        if (outError) {
            *outError = reason;
        }
        return false;
    }
}

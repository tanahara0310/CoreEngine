#pragma once

#include <utility>

namespace CoreEngine
{
    /// @brief 値が違うときだけ代入する
    /// @return 代入したら true
    template <class T, class U>
    bool AssignIfChanged(T& target, U&& value)
    {
        if (target == value) {
            return false;
        }
        target = std::forward<U>(value);
        return true;
    }
}

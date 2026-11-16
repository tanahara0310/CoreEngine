#pragma once

#include <algorithm>
#include <iterator>
#include <utility>

namespace CoreEngine
{
    /// @brief 述語に合う最初の要素を指す（無ければ nullptr）
    /// @note const の範囲には const のポインタを返す
    template <class Range, class Predicate>
    auto FindIfPtr(Range& range, Predicate predicate)
    {
        const auto found = std::find_if(std::begin(range), std::end(range), std::move(predicate));
        return found == std::end(range) ? nullptr : &*found;
    }
}

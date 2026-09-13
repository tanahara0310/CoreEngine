#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief シーン内でオブジェクトを指す ID（0 は未割り当て）
    struct ObjectId
    {
        std::uint64_t value = 0;

        constexpr bool IsValid() const noexcept { return value != 0; }

        friend constexpr bool operator==(ObjectId lhs, ObjectId rhs) noexcept
        {
            return lhs.value == rhs.value;
        }

        /// @brief 16 桁の 16 進文字列にする
        std::string ToString() const;

        /// @brief `ToString()` の形から戻す
        /// @return 読めなければ未割り当て
        static ObjectId FromString(std::string_view text);

        /// @brief 文字列から ID を計算する（64bit FNV-1a。同じ文字列なら常に同じ値）
        /// @return 0 にはならない
        static ObjectId FromKey(std::string_view key);
    };
}

template <>
struct std::hash<CoreEngine::ObjectId>
{
    std::size_t operator()(CoreEngine::ObjectId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

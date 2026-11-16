#pragma once
#include <concepts>
#include <type_traits>

/// @file
/// @brief 整数の切り上げ除算と整列

namespace CoreEngine
{

/// @brief value を divisor で割り、端数を切り上げた値を返す
/// @param value 割られる数（0 以上）
/// @param divisor 割る数（1 以上）
template<std::integral T>
constexpr T DivRoundUp(T value, std::type_identity_t<T> divisor)
{
    return static_cast<T>((value + divisor - 1) / divisor);
}

/// @brief value 以上で alignment の倍数のうち最小の値を返す
/// @param value 整列する値（0 以上）
/// @param alignment 整列の単位（1 以上）
template<std::integral T>
constexpr T AlignUp(T value, std::type_identity_t<T> alignment)
{
    return static_cast<T>(DivRoundUp(value, alignment) * alignment);
}

} // namespace CoreEngine

#pragma once

#include <type_traits>

namespace CoreEngine::EnumFlags
{
    /// @brief 列挙子を元の整数型の値にする
    template <class E>
    constexpr std::underlying_type_t<E> ToUnderlying(E value) noexcept
    {
        static_assert(std::is_enum_v<E>);
        return static_cast<std::underlying_type_t<E>>(value);
    }

    /// @brief 両方のビットを合わせる
    template <class E>
    constexpr E Or(E a, E b) noexcept
    {
        return static_cast<E>(ToUnderlying(a) | ToUnderlying(b));
    }

    /// @brief 両方に立っているビットだけを残す
    template <class E>
    constexpr E And(E a, E b) noexcept
    {
        return static_cast<E>(ToUnderlying(a) & ToUnderlying(b));
    }

    /// @brief flags のビットがすべて立っているか
    template <class E>
    constexpr bool HasAll(E value, E flags) noexcept
    {
        return (ToUnderlying(value) & ToUnderlying(flags)) == ToUnderlying(flags);
    }

    /// @brief flags のビットのどれかが立っているか
    template <class E>
    constexpr bool HasAny(E value, E flags) noexcept
    {
        return (ToUnderlying(value) & ToUnderlying(flags)) != 0;
    }
}

/// @brief フラグの enum に `|` `&` `|=` `&=` と HasFlag（どれかが立っているか）を付ける
/// @note enum と同じ名前空間に書く
#define CE_ENUM_FLAG_OPERATORS(E) \
    constexpr E operator|(E a, E b) noexcept { return ::CoreEngine::EnumFlags::Or(a, b); } \
    constexpr E operator&(E a, E b) noexcept { return ::CoreEngine::EnumFlags::And(a, b); } \
    constexpr E& operator|=(E& a, E b) noexcept { a = a | b; return a; } \
    constexpr E& operator&=(E& a, E b) noexcept { a = a & b; return a; } \
    constexpr bool HasFlag(E value, E flag) noexcept { return ::CoreEngine::EnumFlags::HasAny(value, flag); }

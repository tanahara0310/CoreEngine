#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace CoreEngine::Fnv1a
{
    /// @brief 64bit FNV-1a の初期値
    inline constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;

    /// @brief 64bit FNV-1a の素数
    inline constexpr std::uint64_t kPrime = 1099511628211ull;

    /// @brief バイト列を 1 バイトずつハッシュへ混ぜる
    inline void MixBytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= kPrime;
        }
    }

    /// @brief 値のバイト表現をハッシュへ混ぜる
    template <class T>
    void MixValue(std::uint64_t& hash, const T& value) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        MixBytes(hash, &value, sizeof(T));
    }

    /// @brief 文字列の各文字を unsigned char としてハッシュへ混ぜる
    constexpr void MixString(std::uint64_t& hash, std::string_view text) noexcept
    {
        for (const char c : text) {
            hash ^= static_cast<unsigned char>(c);
            hash *= kPrime;
        }
    }

    /// @brief 文字列の 64bit FNV-1a ハッシュ
    constexpr std::uint64_t Hash(std::string_view text) noexcept
    {
        std::uint64_t hash = kOffsetBasis;
        MixString(hash, text);
        return hash;
    }
}

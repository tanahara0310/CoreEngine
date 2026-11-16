#pragma once

#include <iterator>
#include <string_view>
#include <utility>

namespace CoreEngine
{
    /// @brief 列挙値と保存に使う名前の組（名前の表の 1 行）
    /// @note 同じ値・同じ名前の行が複数あれば先の行を使う（別名は正式な名前の行より後ろに書く）
    template<class E>
    struct EnumName
    {
        E value;
        std::string_view name;
    };

    /// @brief 名前の表（EnumName の C の配列か std::array）の行が持つ値の型
    template<class Table>
    using EnumNameValue = decltype(std::begin(std::declval<const Table&>())->value);

    /// @brief 値の名前を引く（表に無ければ fallback）
    template<class Table>
    constexpr std::string_view NameOf(const Table& table, EnumNameValue<Table> value, std::string_view fallback = {})
    {
        for (const auto& entry : table) {
            if (entry.value == value) {
                return entry.name;
            }
        }
        return fallback;
    }

    /// @brief 名前から値を引く（見つかったときだけ out へ書く）
    /// @return 見つかったら true
    template<class Table>
    constexpr bool TryParseName(const Table& table, std::string_view name, EnumNameValue<Table>& out)
    {
        for (const auto& entry : table) {
            if (entry.name == name) {
                out = entry.value;
                return true;
            }
        }
        return false;
    }

    /// @brief 名前から値を引く（表に無ければ fallback）
    template<class Table>
    constexpr EnumNameValue<Table> ParseName(const Table& table, std::string_view name, EnumNameValue<Table> fallback)
    {
        for (const auto& entry : table) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return fallback;
    }

    /// @brief 保存された番号を列挙値へ戻す（0〜count-1 の外なら fallback）
    template<class E>
    constexpr E ToEnum(int value, int count, E fallback)
    {
        return (value >= 0 && value < count) ? static_cast<E>(value) : fallback;
    }
}

#pragma once

#include "Utility/CVar/CVar.h"

#include <type_traits>

/// @file
/// @brief CVar と設定の構造体のメンバーの対応表

namespace CoreEngine
{
    /// @brief CVar 1 本と構造体のメンバー 1 つの対応
    /// @tparam Owner メンバーを持つ構造体
    /// @tparam Value CVar の値の型
    /// @tparam Member メンバーの型（int の CVar を uint32_t のメンバーへ写すときなどに Value と変える）
    template <class Owner, class Value, class Member = Value>
    struct CVarMemberBinding
    {
        CVar<Value>* cvar;
        Member Owner::* member;

        /// @brief CVar の今の値をメンバーへ写す（符号なしの整数のメンバーへは負の値を 0 にして写す）
        void Load(Owner& owner) const
        {
            const Value& value = cvar->Get();
            if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Member> && !std::is_same_v<Member, bool>) {
                owner.*member = value < Value{ 0 } ? Member{ 0 } : static_cast<Member>(value);
            } else {
                owner.*member = static_cast<Member>(value);
            }
        }

        /// @brief メンバーの値を CVar へ書く（値が変わったときだけ通知が走る）
        void Store(const Owner& owner) const
        {
            cvar->Set(static_cast<Value>(owner.*member));
        }
    };

    /// @brief 表のすべての CVar の今の値を構造体へ写す
    /// @param tables CVarMemberBinding の配列（いくつでも。並びの順に写す）
    template <class Owner, class... Tables>
    void LoadFromCVars(Owner& owner, const Tables&... tables)
    {
        const auto loadTable = [&owner](const auto& table) {
            for (const auto& binding : table) {
                binding.Load(owner);
            }
        };
        (loadTable(tables), ...);
    }

    /// @brief 構造体の値を表のすべての CVar へ書く
    /// @param tables CVarMemberBinding の配列（いくつでも。並びの順に書く）
    template <class Owner, class... Tables>
    void StoreToCVars(const Owner& owner, const Tables&... tables)
    {
        const auto storeTable = [&owner](const auto& table) {
            for (const auto& binding : table) {
                binding.Store(owner);
            }
        };
        (storeTable(tables), ...);
    }
}

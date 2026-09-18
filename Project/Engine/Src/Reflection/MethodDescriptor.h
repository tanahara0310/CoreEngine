#pragma once

#include "Reflection/PropertyDescriptor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace CoreEngine::Reflection
{
    /// @brief 型の操作（メンバ関数）1 つの記述
    /// @details 引数と戻り値に使えるのは bool / int / uint32_t / float / Vector2 / Vector3 / Vector4 / std::string。
    ///          uint32_t は int として受け渡す（負の値は 0 にする）。
    struct MethodDescriptor
    {
        /// @brief 操作を呼ぶ
        /// @param instance 記述子の持ち主（`GetReflectionInstance()` の値）
        /// @param args 引数ごとの値のアドレス（`parameters` の型の実体）
        /// @param result 戻り値の置き場（`returnType` の型の実体。戻り値が無ければ使わない）
        using Invoker = void (*)(void* instance, const void* const* args, void* result);

        const char*               name = "";                     ///< スクリプトから呼ぶ名前
        const char*               displayName = "";              ///< 表示名
        const char*               tooltip = "";                  ///< 説明（空なら出さない）
        bool                      hasReturn = false;             ///< 戻り値があるか
        PropertyType              returnType = PropertyType::Bool;  ///< 戻り値の型（hasReturn のときだけ使う）
        std::vector<PropertyType> parameters;                    ///< 引数の型（並びどおり）
        Invoker                   invoke = nullptr;

        void Invoke(void* instance, const void* const* args, void* result) const
        {
            if (invoke) { invoke(instance, args, result); }
        }
    };

    namespace Detail
    {
        /// @brief 操作の引数・戻り値に使える型と、受け渡しに使う型
        template <class T> struct MethodValue;
        template <> struct MethodValue<bool>          { using Stored = bool;        static constexpr PropertyType kType = PropertyType::Bool; };
        template <> struct MethodValue<int>           { using Stored = int;         static constexpr PropertyType kType = PropertyType::Int; };
        template <> struct MethodValue<std::uint32_t> { using Stored = int;         static constexpr PropertyType kType = PropertyType::Int; };
        template <> struct MethodValue<float>         { using Stored = float;       static constexpr PropertyType kType = PropertyType::Float; };
        template <> struct MethodValue<Vector2>       { using Stored = Vector2;     static constexpr PropertyType kType = PropertyType::Vector2; };
        template <> struct MethodValue<Vector3>       { using Stored = Vector3;     static constexpr PropertyType kType = PropertyType::Vector3; };
        template <> struct MethodValue<Vector4>       { using Stored = Vector4;     static constexpr PropertyType kType = PropertyType::Vector4; };
        template <> struct MethodValue<std::string>   { using Stored = std::string; static constexpr PropertyType kType = PropertyType::String; };

        template <class T>
        using MethodBare = std::remove_cvref_t<T>;

        template <class T>
        using MethodStored = typename MethodValue<MethodBare<T>>::Stored;

        /// @brief 受け渡しの型の値を、引数の型へ直す（uint32_t の負の値は 0 にする）
        template <class T>
        decltype(auto) FromStored(const MethodStored<T>& value)
        {
            if constexpr (std::is_same_v<MethodBare<T>, std::uint32_t>) {
                return value < 0 ? std::uint32_t{ 0 } : static_cast<std::uint32_t>(value);
            } else {
                return (value);
            }
        }

        template <class... A> struct TypeList {};

        template <class F> struct MemberTraits;

        template <class C, class R, class... A>
        struct MemberTraits<R (C::*)(A...)>
        {
            using Return = R;
            using Args = TypeList<A...>;
        };

        template <class C, class R, class... A>
        struct MemberTraits<R (C::*)(A...) const>
        {
            using Return = R;
            using Args = TypeList<A...>;
        };

        template <class Self, auto Function, class... A, std::size_t... I>
        void InvokeWith(void* instance, const void* const* args, void* result, TypeList<A...>, std::index_sequence<I...>)
        {
            using Return = typename MemberTraits<decltype(Function)>::Return;
            Self& self = *static_cast<Self*>(instance);
            if constexpr (std::is_void_v<Return>) {
                (self.*Function)(FromStored<A>(*static_cast<const MethodStored<A>*>(args[I]))...);
                (void)result;
            } else {
                *static_cast<MethodStored<Return>*>(result) = static_cast<MethodStored<Return>>(
                    (self.*Function)(FromStored<A>(*static_cast<const MethodStored<A>*>(args[I]))...));
            }
            (void)args;
        }

        template <class Self, auto Function, class... A>
        void InvokeList(void* instance, const void* const* args, void* result, TypeList<A...> list)
        {
            InvokeWith<Self, Function>(instance, args, result, list, std::index_sequence_for<A...>{});
        }

        template <class Self, auto Function>
        void Invoke(void* instance, const void* const* args, void* result)
        {
            InvokeList<Self, Function>(instance, args, result, typename MemberTraits<decltype(Function)>::Args{});
        }

        template <class... A>
        void AppendParameters(std::vector<PropertyType>& out, TypeList<A...>)
        {
            (out.push_back(MethodValue<MethodBare<A>>::kType), ...);
        }
    }

    /// @brief メンバ関数から操作の記述を作る（REFLECT_METHOD が使う）
    /// @tparam Self 記述子の持ち主の型（`GetReflectionInstance()` が指す型）
    /// @tparam Function Self か基底の公開メンバ関数（多重定義しているものは使えない）
    template <class Self, auto Function>
    MethodDescriptor MakeMethod(const char* name, const char* displayName)
    {
        using Traits = Detail::MemberTraits<decltype(Function)>;
        MethodDescriptor method;
        method.name = name;
        method.displayName = displayName;
        if constexpr (!std::is_void_v<typename Traits::Return>) {
            method.hasReturn = true;
            method.returnType = Detail::MethodValue<Detail::MethodBare<typename Traits::Return>>::kType;
        }
        Detail::AppendParameters(method.parameters, typename Traits::Args{});
        method.invoke = &Detail::Invoke<Self, Function>;
        return method;
    }
}

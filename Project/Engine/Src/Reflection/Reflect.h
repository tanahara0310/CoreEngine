#pragma once

#include "Reflection/TypeDescriptor.h"
#include "Utility/Macro/UniqueName.h"

#include <string>
#include <type_traits>
#include <utility>

/// @file
/// @brief 型のプロパティを宣言するマクロ
///
/// クラスの public セクションに書く:
/// @code
///     REFLECT_BEGIN(TransformComponent, "トランスフォーム")
///         REFLECT_PROPERTY(transform_.translate, "位置", Speed(0.05f))
///         REFLECT_PROPERTY(transform_.scale,     "スケール", Range(0.001f, 1000.0f))
///         REFLECT_READONLY(cachedWorldY_, "ワールド Y")
///     REFLECT_END()
/// @endcode
///
/// 対応する .cpp のファイルスコープで REFLECT_REGISTER(型名) を書くと
/// 起動時に TypeRegistry へ載る。
///
/// メンバ式をそのまま渡すので、名前を間違えるとコンパイルエラーになる。
/// 値の型はその式から推論するため型名は書かない。

namespace CoreEngine::Reflection
{
    /// @brief メンバ式から保存キーを導き出す
    /// @param memberExpr `transform_.translate` のような式の綴り
    /// @return 最後の `.` `->` `::` より後ろを取り、末尾のアンダースコアを落としたもの
    /// @note 綴りを変えると保存キーも変わる。既存の JSON と合わせたい場合は
    ///       REFLECT_PROPERTY の可変長引数で `p.name = "texture"` と上書きする。
    inline std::string DerivePropertyName(const char* memberExpr)
    {
        std::string s = memberExpr ? memberExpr : "";
        const size_t separator = s.find_last_of(".>:");
        if (separator != std::string::npos) {
            s.erase(0, separator + 1);
        }
        while (!s.empty() && s.back() == '_') {
            s.pop_back();
        }
        return s;
    }

    /// @brief ドラッグ速度だけを指定する
    constexpr PropertyRange Speed(float dragSpeed)
    {
        PropertyRange r;
        r.speed = dragSpeed;
        return r;
    }

    /// @brief 下限・上限を指定する
    constexpr PropertyRange Range(float minValue, float maxValue, float dragSpeed = 0.0f)
    {
        return PropertyRange{ minValue, maxValue, dragSpeed };
    }

    /// @brief REFLECT_BEGIN が作る記述子を初回参照時に 1 度だけ組み立てる
    template <class T>
    struct TypeDescriptorHolder
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = T::BuildTypeDescriptor();
            return descriptor;
        }
    };

    /// @brief 静的初期化のタイミングでレジストリへ登録する
    template <class T>
    struct AutoRegister
    {
        AutoRegister() { TypeRegistry::Get().Register(&TypeDescriptorHolder<T>::Get()); }
    };
}

#define REFLECT_BEGIN(TypeName, DisplayNameLiteral)                                    \
    using ReflectedSelf = TypeName;                                                    \
    static ::CoreEngine::Reflection::TypeDescriptor BuildTypeDescriptor()              \
    {                                                                                  \
        using Self = TypeName;                                                         \
        using ::CoreEngine::Reflection::Speed;                                         \
        using ::CoreEngine::Reflection::Range;                                         \
        ::CoreEngine::Reflection::TypeDescriptor d;                                    \
        d.name = #TypeName;                                                            \
        d.displayName = DisplayNameLiteral;

/// @brief メンバ式から読み書きの口を組み立てる（他のマクロが使う）
#define REFLECT_DETAIL_MEMBER_ACCESS(MemberExpr)                                       \
            p.get = [](const void* o, void* out) {                                     \
                *static_cast<ValueType*>(out) = static_cast<const Self*>(o)->MemberExpr; \
            };                                                                         \
            p.set = [](void* o, const void* in) {                                      \
                static_cast<Self*>(o)->MemberExpr = *static_cast<const ValueType*>(in); \
            };

/// @brief 編集できるプロパティを足す
/// @param MemberExpr インスタンスからの式（`value_` や `transform_.translate`）
/// @param ... 省略可。`Speed(0.05f)` または `Range(0.0f, 1.0f)`
#define REFLECT_PROPERTY(MemberExpr, DisplayNameLiteral, ...)                          \
        {                                                                              \
            using ValueType =                                                          \
                ::std::decay_t<decltype(::std::declval<Self&>().MemberExpr)>;          \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = ::CoreEngine::Reflection::DerivePropertyName(#MemberExpr);        \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyTypeOf<ValueType>::kValue;       \
            REFLECT_DETAIL_MEMBER_ACCESS(MemberExpr)                                   \
            __VA_ARGS__;                                                               \
            d.properties.push_back(p);                                                 \
        }

/// @brief getter / setter で表すプロパティを足す
/// @param NameLiteral 保存キー（メンバ式が無いので明示する）
/// @param GetterName 値を返すメンバ関数（引数なし）
/// @param SetterName 値を受け取るメンバ関数（引数 1 つ）
/// @param ... 省略可。`Speed(0.05f)` または `Range(0.0f, 1.0f)`
/// @note 値を自分で持たない型のためのもの。`MaterialComponent` の色のように
///       実体が別クラス側にあるものは、メンバ式では表せない。
#define REFLECT_ACCESSOR(NameLiteral, DisplayNameLiteral, GetterName, SetterName, ...) \
        {                                                                              \
            using ValueType =                                                          \
                ::std::decay_t<decltype(::std::declval<const Self&>().GetterName())>;  \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = NameLiteral;                                                      \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyTypeOf<ValueType>::kValue;       \
            p.get = [](const void* o, void* out) {                                     \
                *static_cast<ValueType*>(out) = static_cast<const Self*>(o)->GetterName(); \
            };                                                                         \
            p.set = [](void* o, const void* in) {                                      \
                static_cast<Self*>(o)->SetterName(*static_cast<const ValueType*>(in)); \
            };                                                                         \
            __VA_ARGS__;                                                               \
            d.properties.push_back(p);                                                 \
        }

/// @brief 編集も保存もしない表示専用のプロパティを足す
#define REFLECT_READONLY(MemberExpr, DisplayNameLiteral)                               \
        {                                                                              \
            using ValueType =                                                          \
                ::std::decay_t<decltype(::std::declval<Self&>().MemberExpr)>;          \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = ::CoreEngine::Reflection::DerivePropertyName(#MemberExpr);        \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyTypeOf<ValueType>::kValue;       \
            REFLECT_DETAIL_MEMBER_ACCESS(MemberExpr)                                   \
            p.flags = ::CoreEngine::Reflection::PropertyFlags::ReadOnly;                \
            d.properties.push_back(p);                                                 \
        }

/// @brief 読み取り専用の派生値を足す（メンバでなく計算結果）
/// @param NameLiteral 識別子
/// @param GetterName 値を返すメンバ関数（引数なし）
#define REFLECT_READONLY_ACCESSOR(NameLiteral, DisplayNameLiteral, GetterName)         \
        {                                                                              \
            using ValueType =                                                          \
                ::std::decay_t<decltype(::std::declval<const Self&>().GetterName())>;  \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = NameLiteral;                                                      \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyTypeOf<ValueType>::kValue;       \
            p.get = [](const void* o, void* out) {                                     \
                *static_cast<ValueType*>(out) = static_cast<const Self*>(o)->GetterName(); \
            };                                                                         \
            p.set = [](void*, const void*) {};                                         \
            p.flags = ::CoreEngine::Reflection::PropertyFlags::ReadOnly;                \
            d.properties.push_back(p);                                                 \
        }

#define REFLECT_END()                                                                  \
        return d;                                                                      \
    }                                                                                  \
    const ::CoreEngine::Reflection::TypeDescriptor* GetTypeDescriptor() const override  \
    {                                                                                  \
        return &::CoreEngine::Reflection::TypeDescriptorHolder<ReflectedSelf>::Get();   \
    }                                                                                  \
    void* GetReflectionInstance() override                                             \
    {                                                                                  \
        return static_cast<ReflectedSelf*>(this);                                      \
    }

/// @brief 型を TypeRegistry へ登録する（対応する .cpp のファイルスコープに書く）
#define REFLECT_REGISTER(TypeName)                                                     \
    namespace {                                                                        \
        const ::CoreEngine::Reflection::AutoRegister<TypeName>                         \
            CORE_UNIQUE_NAME(kAutoRegisterType_){};                                    \
    }

#pragma once

#include "Reflection/TypeDescriptor.h"
#include "Utility/Macro/UniqueName.h"

#include <iterator>
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
    ///       REFLECT_PROPERTY の可変長引数で `p.name = "texture"` と上書きするか、
    ///       REFLECT_VERSION で版を上げて REFLECT_RENAMED で古いキーを移行表に足す。
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

    /// @brief ラジアンで持つ値を度で見せるときの `displayScale`
    inline constexpr float kDegreesPerRadian = 180.0f / 3.14159265358979f;

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
        using ::CoreEngine::Reflection::kDegreesPerRadian;                             \
        ::CoreEngine::Reflection::TypeDescriptor d;                                    \
        d.name = #TypeName;                                                            \
        d.displayName = DisplayNameLiteral;

/// @brief メンバ式から読み書きの口を組み立てる（他のマクロが使う）
#define REFLECT_DETAIL_MEMBER_ACCESS(MemberExpr)                                       \
            p.get = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       const void* o, void* out) {                                     \
                *static_cast<ValueType*>(out) = static_cast<const Self*>(o)->MemberExpr; \
            };                                                                         \
            p.set = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       void* o, const void* in) {                                      \
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
            p.get = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       const void* o, void* out) {                                     \
                *static_cast<ValueType*>(out) = static_cast<const Self*>(o)->GetterName(); \
            };                                                                         \
            p.set = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       void* o, const void* in) {                                      \
                static_cast<Self*>(o)->SetterName(*static_cast<const ValueType*>(in)); \
            };                                                                         \
            __VA_ARGS__;                                                               \
            d.properties.push_back(p);                                                 \
        }

/// @brief 列挙型の値を getter / setter で表し、名前の一覧から選ぶプロパティを足す
/// @param NameLiteral 保存キー
/// @param GetterName 列挙型の値を返すメンバ関数（引数なし）
/// @param SetterName 列挙型の値を受け取るメンバ関数（引数 1 つ）
/// @param NamesArray 列挙の値の順に並べた名前の配列
/// @param ... 省略可。`p.tooltip = "…"` など
/// @note 値は整数として保存する。名前の数の範囲外の整数は書き込まない。
#define REFLECT_ENUM_ACCESSOR(NameLiteral, DisplayNameLiteral, GetterName, SetterName, NamesArray, ...) \
        {                                                                              \
            using EnumType =                                                           \
                ::std::decay_t<decltype(::std::declval<const Self&>().GetterName())>;  \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = NameLiteral;                                                      \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyType::Int;                      \
            p.enumNames = NamesArray;                                                  \
            p.enumCount = static_cast<int>(::std::size(NamesArray));                   \
            p.get = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       const void* o, void* out) {                                     \
                *static_cast<int*>(out) =                                              \
                    static_cast<int>(static_cast<const Self*>(o)->GetterName());       \
            };                                                                         \
            p.set = [](const ::CoreEngine::Reflection::PropertyDescriptor& property,   \
                       void* o, const void* in) {                                      \
                const int value = *static_cast<const int*>(in);                        \
                if (value < 0 || value >= property.enumCount) {                        \
                    return;                                                            \
                }                                                                      \
                static_cast<Self*>(o)->SetterName(static_cast<EnumType>(value));        \
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
            p.get = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       const void* o, void* out) {                                     \
                *static_cast<ValueType*>(out) = static_cast<const Self*>(o)->GetterName(); \
            };                                                                         \
            p.set = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       void*, const void*) {};                                         \
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

/// @brief 記述子の組み立てを .cpp に置く型の宣言（クラスの public セクションに書く）
/// @note 対応する .cpp のファイルスコープに REFLECT_DEFINE_BEGIN 〜 REFLECT_DEFINE_END を書く。
///       プロパティの型がヘッダでは前方宣言だけのとき（`ObjectRef<T>` の `T` など）に使う。
#define REFLECT_DECLARE(TypeName)                                                      \
    using ReflectedSelf = TypeName;                                                    \
    static constexpr const char* kReflectedTypeName = #TypeName;                       \
    static ::CoreEngine::Reflection::TypeDescriptor BuildTypeDescriptor();             \
    const ::CoreEngine::Reflection::TypeDescriptor* GetTypeDescriptor() const override  \
    {                                                                                  \
        return &::CoreEngine::Reflection::TypeDescriptorHolder<ReflectedSelf>::Get();   \
    }                                                                                  \
    void* GetReflectionInstance() override                                             \
    {                                                                                  \
        return static_cast<ReflectedSelf*>(this);                                      \
    }

/// @brief REFLECT_DECLARE した型の記述子を組み立てる
/// @param QualifiedTypeName 名前空間を含めた型名
#define REFLECT_DEFINE_BEGIN(QualifiedTypeName, DisplayNameLiteral)                    \
    ::CoreEngine::Reflection::TypeDescriptor QualifiedTypeName::BuildTypeDescriptor()  \
    {                                                                                  \
        using Self = QualifiedTypeName;                                                \
        using ::CoreEngine::Reflection::Speed;                                         \
        using ::CoreEngine::Reflection::Range;                                         \
        using ::CoreEngine::Reflection::kDegreesPerRadian;                             \
        ::CoreEngine::Reflection::TypeDescriptor d;                                    \
        d.name = Self::kReflectedTypeName;                                             \
        d.displayName = DisplayNameLiteral;

#define REFLECT_DEFINE_END()                                                           \
        return d;                                                                      \
    }

/// @brief 記述子がプロパティの一部だけを持つことを示す
/// @note 残りの保存と表示は `OnSerialize` / `OnDeserialize` / `DrawInspector` が受け持つ。
#define REFLECT_PARTIAL()                                                              \
        d.partial = true;

/// @brief 型の保存形の版を指定する（書かなければ 1）
/// @note 版を上げたら、上げた版ごとに REFLECT_RENAMED か REFLECT_UPGRADE で移行を書く。
///       移行の無い版があると、起動時に TypeRegistry がエラーを出す。
#define REFLECT_VERSION(Number)                                                        \
        d.version = Number;

/// @brief 保存キーの改名を移行表に足す
/// @param Version 新しいキーで保存するようになった版
/// @param OldNameLiteral それより前の版での保存キー
/// @param NewNameLiteral その版からの保存キー
/// @note 古い版で保存した JSON は、値を流す前にキーを付け替える。
#define REFLECT_RENAMED(Version, OldNameLiteral, NewNameLiteral)                       \
        d.renames.push_back(::CoreEngine::Reflection::KeyRename{                       \
            Version, OldNameLiteral, NewNameLiteral });

/// @brief キーの改名だけでは表せない保存値の書き換えを指定する
/// @param FunctionName `void (uint32_t fromVersion, uint32_t toVersion, json& parameters)` の関数
/// @note 1 版ずつ、その版の改名を済ませてから呼ぶ。`toVersion` の版で変わった分だけを書き換える。
#define REFLECT_UPGRADE(FunctionName)                                                  \
        d.upgrade = &FunctionName;

/// @brief `ObjectRef<T>` 型のメンバをプロパティとして足す
/// @param MemberExpr インスタンスからの式（`railPath_` など）
#define REFLECT_OBJECT_REF(MemberExpr, DisplayNameLiteral)                             \
        {                                                                              \
            using RefType =                                                            \
                ::std::decay_t<decltype(::std::declval<Self&>().MemberExpr)>;          \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = ::CoreEngine::Reflection::DerivePropertyName(#MemberExpr);        \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyType::ObjectRef;                \
            p.get = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       const void* o, void* out) {                                     \
                *static_cast<::CoreEngine::Reflection::ObjectRefValue*>(out) =         \
                    static_cast<const Self*>(o)->MemberExpr.GetValue();                \
            };                                                                         \
            p.set = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       void* o, const void* in) {                                      \
                Self* self = static_cast<Self*>(o);                                    \
                self->MemberExpr.SetValue(                                             \
                    *static_cast<const ::CoreEngine::Reflection::ObjectRefValue*>(in), \
                    self);                                                             \
            };                                                                         \
            p.acceptsComponent = &RefType::Accepts;                                    \
            d.properties.push_back(p);                                                 \
        }

/// @brief `AssetRef<T>` 型のメンバをプロパティとして足す
/// @param MemberExpr インスタンスからの式（`buildSe_` など）
#define REFLECT_ASSET_REF(MemberExpr, DisplayNameLiteral)                              \
        {                                                                              \
            using RefType =                                                            \
                ::std::decay_t<decltype(::std::declval<Self&>().MemberExpr)>;          \
            ::CoreEngine::Reflection::PropertyDescriptor p;                            \
            p.name = ::CoreEngine::Reflection::DerivePropertyName(#MemberExpr);        \
            p.displayName = DisplayNameLiteral;                                        \
            p.type = ::CoreEngine::Reflection::PropertyType::AssetRef;                 \
            p.get = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       const void* o, void* out) {                                     \
                *static_cast<::CoreEngine::Reflection::AssetRefValue*>(out) =          \
                    static_cast<const Self*>(o)->MemberExpr.GetValue();                \
            };                                                                         \
            p.set = [](const ::CoreEngine::Reflection::PropertyDescriptor&,            \
                       void* o, const void* in) {                                      \
                static_cast<Self*>(o)->MemberExpr.SetValue(                            \
                    *static_cast<const ::CoreEngine::Reflection::AssetRefValue*>(in)); \
            };                                                                         \
            p.assetType = RefType::kAssetType;                                         \
            d.properties.push_back(p);                                                 \
        }

/// @brief 型を TypeRegistry へ登録する（対応する .cpp のファイルスコープに書く）
#define REFLECT_REGISTER(TypeName)                                                     \
    namespace {                                                                        \
        const ::CoreEngine::Reflection::AutoRegister<TypeName>                         \
            CORE_UNIQUE_NAME(kAutoRegisterType_){};                                    \
    }

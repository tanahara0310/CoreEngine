#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <cstdint>
#include <string>
#include <type_traits>

namespace CoreEngine::Reflection
{
    /// @brief プロパティが保持する値の型
    enum class PropertyType
    {
        Bool,
        Int,
        Float,
        Vector2,
        Vector3,
        Vector4,
        Color,
        String,
    };

    /// @brief 数値プロパティの編集範囲
    struct PropertyRange
    {
        float min = 0.0f;
        float max = 0.0f;
        float speed = 0.0f;
        bool  valid = false;

        constexpr PropertyRange() = default;
        constexpr PropertyRange(float minValue, float maxValue, float dragSpeed = 0.0f)
            : min(minValue), max(maxValue), speed(dragSpeed), valid(true) {}
    };

    /// @brief プロパティの振る舞い
    enum class PropertyFlags : uint32_t
    {
        None     = 0,
        ReadOnly = 1 << 0,  ///< インスペクタで編集させず、保存もしない
        Hidden   = 1 << 1,  ///< インスペクタに出さない（保存はする）
        NoSave   = 1 << 2,  ///< 保存しない（インスペクタには出す）
    };

    constexpr PropertyFlags operator|(PropertyFlags a, PropertyFlags b) noexcept
    {
        return static_cast<PropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    constexpr bool HasFlag(PropertyFlags value, PropertyFlags flag) noexcept
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    /// @brief C++ の型から PropertyType を引く
    template <class T> struct PropertyTypeOf;
    template <> struct PropertyTypeOf<bool>        { static constexpr PropertyType kValue = PropertyType::Bool; };
    template <> struct PropertyTypeOf<int>         { static constexpr PropertyType kValue = PropertyType::Int; };
    template <> struct PropertyTypeOf<float>       { static constexpr PropertyType kValue = PropertyType::Float; };
    template <> struct PropertyTypeOf<Vector2>     { static constexpr PropertyType kValue = PropertyType::Vector2; };
    template <> struct PropertyTypeOf<Vector3>     { static constexpr PropertyType kValue = PropertyType::Vector3; };
    template <> struct PropertyTypeOf<Vector4>     { static constexpr PropertyType kValue = PropertyType::Vector4; };
    template <> struct PropertyTypeOf<std::string> { static constexpr PropertyType kValue = PropertyType::String; };

    /// @brief 型ごとの値サイズ（Undo のスナップショットが使う）
    size_t SizeOfPropertyType(PropertyType type) noexcept;

    /// @brief 1 つのプロパティの記述
    struct PropertyDescriptor
    {
        /// @brief インスタンスの先頭から値へのポインタを求める
        using Resolver = void* (*)(void*);

        /// @brief 保存キー兼 UI の識別子（既定はメンバ式の末尾トークン）
        std::string   name;
        const char*   displayName = "";
        PropertyType  type = PropertyType::Float;
        Resolver      resolve = nullptr;
        PropertyRange range{};
        PropertyFlags flags = PropertyFlags::None;

        void* ValuePtr(void* instance) const { return resolve ? resolve(instance) : nullptr; }
        const void* ValuePtr(const void* instance) const
        {
            return resolve ? resolve(const_cast<void*>(instance)) : nullptr;
        }

        bool IsEditable() const { return !HasFlag(flags, PropertyFlags::ReadOnly); }
        bool IsVisible()  const { return !HasFlag(flags, PropertyFlags::Hidden); }
        bool IsSaved()    const
        {
            return !HasFlag(flags, PropertyFlags::NoSave) && !HasFlag(flags, PropertyFlags::ReadOnly);
        }
    };
}

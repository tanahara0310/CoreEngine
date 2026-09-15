#pragma once

#include "GameObject/ObjectId.h"
#include "Graphics/Asset/AssetType.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace CoreEngine
{
    class IComponent;
}

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
        ObjectRef,
        AssetRef,
        Array,
    };

    /// @brief シーン内の別オブジェクトのコンポーネントを指す値
    struct ObjectRefValue
    {
        ObjectId    objectId{};
        std::string componentType;  ///< 指す先の `IComponent::GetTypeName()`

        bool operator==(const ObjectRefValue& other) const
        {
            return objectId == other.objectId && componentType == other.componentType;
        }
    };

    /// @brief ディスク上のアセットを指す値
    struct AssetRefValue
    {
        std::string guid;  ///< `.meta` の GUID
        std::string path;  ///< プロジェクトの根からの相対パス（区切りは `/`）

        bool operator==(const AssetRefValue& other) const
        {
            return guid == other.guid && path == other.path;
        }
    };

    /// @brief 配列の値（要素の型は記述子の `elementType`）
    struct ArrayValue
    {
        /// @brief 要素 1 つ分の値（Color の要素は Vector4 で持つ）
        using Element = std::variant<bool, int, float, Vector2, Vector3, Vector4, std::string>;

        std::vector<Element> elements;
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
    template <> struct PropertyTypeOf<bool>           { static constexpr PropertyType kValue = PropertyType::Bool; };
    template <> struct PropertyTypeOf<int>            { static constexpr PropertyType kValue = PropertyType::Int; };
    template <> struct PropertyTypeOf<float>          { static constexpr PropertyType kValue = PropertyType::Float; };
    template <> struct PropertyTypeOf<Vector2>        { static constexpr PropertyType kValue = PropertyType::Vector2; };
    template <> struct PropertyTypeOf<Vector3>        { static constexpr PropertyType kValue = PropertyType::Vector3; };
    template <> struct PropertyTypeOf<Vector4>        { static constexpr PropertyType kValue = PropertyType::Vector4; };
    template <> struct PropertyTypeOf<std::string>    { static constexpr PropertyType kValue = PropertyType::String; };
    template <> struct PropertyTypeOf<ObjectRefValue> { static constexpr PropertyType kValue = PropertyType::ObjectRef; };
    template <> struct PropertyTypeOf<AssetRefValue>  { static constexpr PropertyType kValue = PropertyType::AssetRef; };

    /// @brief 型ごとの値サイズ（Undo のスナップショットが使う）
    size_t SizeOfPropertyType(PropertyType type) noexcept;

    /// @brief 配列の要素にできる型か（Bool / Int / Float / Vector2 / Vector3 / Vector4 / Color / String）
    bool IsArrayElementType(PropertyType type) noexcept;

    /// @brief 1 つのプロパティの記述
    struct PropertyDescriptor
    {
        /// @brief 値を読み出す
        /// @param property 読み出すプロパティ（`index` を使う口が読む）
        /// @param out `type` に対応する型の実体。呼び出し側が用意する
        /// @note 値のアドレスを返す形にしないのは、値を自分で持たない型を表せないため。
        ///       `MaterialComponent` の色は `MaterialInstance` 側にあり、
        ///       `MeshRendererComponent` のブレンドモードは Get/Set 越しにしか触れない。
        using Getter = void (*)(const PropertyDescriptor& property, const void* instance, void* out);

        /// @brief 値を書き込む
        /// @param property 書き込むプロパティ（`index` を使う口が読む）
        /// @param in `type` に対応する型の実体
        using Setter = void (*)(const PropertyDescriptor& property, void* instance, const void* in);

        /// @brief ObjectRef が指せるコンポーネントかを判定する
        using ComponentFilter = bool (*)(const IComponent* component);

        /// @brief 保存キー兼 UI の識別子（既定はメンバ式の末尾トークン）
        std::string     name;
        const char*     displayName = "";
        const char*     tooltip = "";  ///< インスペクタで項目にカーソルを乗せたときに出す説明（空なら出さない）
        PropertyType    type = PropertyType::Float;
        Getter          get = nullptr;
        Setter          set = nullptr;
        uint32_t        index = 0;  ///< 読み書きの口が使う番号（スクリプトのクラスのメンバ変数の添え字）
        PropertyRange   range{};
        PropertyFlags   flags = PropertyFlags::None;
        ComponentFilter acceptsComponent = nullptr;  ///< ObjectRef の繋ぎ先の判定（ObjectRef 以外は nullptr）
        AssetType       assetType = AssetType::Unknown;  ///< AssetRef が指せるアセットの種類（AssetRef 以外は Unknown）
        PropertyType    elementType = PropertyType::Float;  ///< Array の要素の型（Array 以外は使わない）

        /// @brief 読み書きの口が揃っているか
        bool IsValid() const { return get != nullptr && set != nullptr; }

        void Get(const void* instance, void* out) const { if (get) { get(*this, instance, out); } }
        void Set(void* instance, const void* in) const { if (set) { set(*this, instance, in); } }

        bool IsEditable() const { return !HasFlag(flags, PropertyFlags::ReadOnly); }
        bool IsVisible()  const { return !HasFlag(flags, PropertyFlags::Hidden); }
        bool IsSaved()    const
        {
            return !HasFlag(flags, PropertyFlags::NoSave) && !HasFlag(flags, PropertyFlags::ReadOnly);
        }
    };
}

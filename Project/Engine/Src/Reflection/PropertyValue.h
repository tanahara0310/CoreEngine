#pragma once

#include "Reflection/PropertyDescriptor.h"

#include <string>
#include <variant>

namespace CoreEngine::Reflection
{
    /// @brief プロパティ 1 つ分の値のコピー
    /// @details Undo の「編集前の値」を持ち運ぶために使う。
    ///          `std::string` を含むので、生バイトのコピーでは扱えない。
    class PropertyValue
    {
    public:
        /// @brief インスタンス上の値をコピーして持つ
        void CopyFrom(PropertyType type, const void* source);

        /// @brief 記述子の getter で読み出して持つ
        void LoadFrom(const PropertyDescriptor& property, const void* instance);

        /// @brief 記述子の setter で書き戻す
        /// @return 型が一致して書き戻せたら true
        bool StoreTo(const PropertyDescriptor& property, void* instance) const;

        /// @brief 持っている値を直接編集するためのポインタ
        /// @return 型が一致しなければ nullptr。ImGui のウィジェットへ渡す用
        void* Data(PropertyType type);
        const void* Data(PropertyType type) const;

        /// @brief 持っている値をインスタンスへ書き戻す
        /// @return 型が一致して書き戻せたら true
        bool ApplyTo(PropertyType type, void* destination) const;

        /// @brief 持っている値が引数と同じか
        bool Equals(PropertyType type, const void* other) const;

        /// @brief 値を持っているか
        bool IsValid() const noexcept { return !std::holds_alternative<std::monostate>(value_); }

        void Reset() { value_ = std::monostate{}; }

    private:
        std::variant<std::monostate, bool, int, float,
                     Vector2, Vector3, Vector4, std::string, ObjectRefValue> value_;
    };
}

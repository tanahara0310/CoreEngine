#include "pch.h"
#include "Reflection/PropertyValue.h"

namespace CoreEngine::Reflection
{
    namespace
    {
        /// @brief 型ごとの処理を 1 か所に集めるための振り分け
        /// @param fn `T` を型引数に取る呼び出し可能物（Color は Vector4 として渡る）
        template <class Fn>
        auto Dispatch(PropertyType type, Fn&& fn)
        {
            switch (type) {
            case PropertyType::Bool:    return fn(static_cast<bool*>(nullptr));
            case PropertyType::Int:     return fn(static_cast<int*>(nullptr));
            case PropertyType::Float:   return fn(static_cast<float*>(nullptr));
            case PropertyType::Vector2: return fn(static_cast<Vector2*>(nullptr));
            case PropertyType::Vector3: return fn(static_cast<Vector3*>(nullptr));
            case PropertyType::Vector4:
            case PropertyType::Color:   return fn(static_cast<Vector4*>(nullptr));
            case PropertyType::String:  return fn(static_cast<std::string*>(nullptr));
            }
            return fn(static_cast<float*>(nullptr));
        }

        bool IsSame(const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }
        bool IsSame(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
        bool IsSame(const Vector4& a, const Vector4& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }
        template <class T> bool IsSame(const T& a, const T& b) { return a == b; }
    }

    void PropertyValue::CopyFrom(PropertyType type, const void* source)
    {
        if (!source) {
            Reset();
            return;
        }
        Dispatch(type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            value_ = *static_cast<const T*>(source);
            });
    }

    bool PropertyValue::ApplyTo(PropertyType type, void* destination) const
    {
        if (!destination || !IsValid()) {
            return false;
        }
        return Dispatch(type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            const T* stored = std::get_if<T>(&value_);
            if (!stored) { return false; }
            *static_cast<T*>(destination) = *stored;
            return true;
            });
    }

    bool PropertyValue::Equals(PropertyType type, const void* other) const
    {
        if (!other || !IsValid()) {
            return false;
        }
        return Dispatch(type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            const T* stored = std::get_if<T>(&value_);
            if (!stored) { return false; }
            return IsSame(*stored, *static_cast<const T*>(other));
            });
    }
}

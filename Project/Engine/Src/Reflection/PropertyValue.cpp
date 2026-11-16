#include "pch.h"
#include "Reflection/PropertyValue.h"

#include <utility>

namespace CoreEngine::Reflection
{
    namespace
    {
        bool IsSame(const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }
        bool IsSame(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
        bool IsSame(const Vector4& a, const Vector4& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }
        template <class T> bool IsSame(const T& a, const T& b) { return a == b; }

        bool IsSame(const ArrayValue& a, const ArrayValue& b)
        {
            if (a.elements.size() != b.elements.size()) {
                return false;
            }
            for (size_t i = 0; i < a.elements.size(); ++i) {
                const ArrayValue::Element& other = b.elements[i];
                if (a.elements[i].index() != other.index()) {
                    return false;
                }
                const bool same = std::visit([&](const auto& value) {
                    return IsSame(value, std::get<std::decay_t<decltype(value)>>(other));
                    }, a.elements[i]);
                if (!same) {
                    return false;
                }
            }
            return true;
        }
    }

    void PropertyValue::CopyFrom(PropertyType type, const void* source)
    {
        if (!source) {
            Reset();
            return;
        }
        VisitPropertyType(type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            value_ = *static_cast<const T*>(source);
            });
    }

    bool PropertyValue::ApplyTo(PropertyType type, void* destination) const
    {
        if (!destination || !IsValid()) {
            return false;
        }
        return VisitPropertyType(type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            const T* stored = std::get_if<T>(&value_);
            if (!stored) { return false; }
            *static_cast<T*>(destination) = *stored;
            return true;
            });
    }

    void PropertyValue::LoadFrom(const PropertyDescriptor& property, const void* instance)
    {
        if (!instance || !property.get) {
            Reset();
            return;
        }
        VisitPropertyType(property.type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            T temp{};
            property.Get(instance, &temp);
            value_ = std::move(temp);
            });
    }

    bool PropertyValue::StoreTo(const PropertyDescriptor& property, void* instance) const
    {
        if (!instance || !property.set || !IsValid()) {
            return false;
        }
        return VisitPropertyType(property.type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            const T* stored = std::get_if<T>(&value_);
            if (!stored) { return false; }
            property.Set(instance, stored);
            return true;
            });
    }

    void* PropertyValue::Data(PropertyType type)
    {
        return const_cast<void*>(std::as_const(*this).Data(type));
    }

    const void* PropertyValue::Data(PropertyType type) const
    {
        return VisitPropertyType(type, [&](auto* tag) -> const void* {
            using T = std::remove_pointer_t<decltype(tag)>;
            return std::get_if<T>(&value_);
            });
    }

    bool PropertyValue::Equals(PropertyType type, const void* other) const
    {
        if (!other || !IsValid()) {
            return false;
        }
        return VisitPropertyType(type, [&](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            const T* stored = std::get_if<T>(&value_);
            if (!stored) { return false; }
            return IsSame(*stored, *static_cast<const T*>(other));
            });
    }

    void* ArrayElementData(PropertyType elementType, ArrayValue::Element& element)
    {
        return const_cast<void*>(ArrayElementData(elementType, std::as_const(element)));
    }

    const void* ArrayElementData(PropertyType elementType, const ArrayValue::Element& element)
    {
        return VisitPropertyType(elementType, [&](auto* tag) -> const void* {
            using T = std::remove_pointer_t<decltype(tag)>;
            if constexpr (kIsAlternativeOf<T, ArrayValue::Element>) {
                return std::get_if<T>(&element);
            } else {
                return nullptr;
            }
            });
    }

    ArrayValue::Element MakeArrayElement(PropertyType elementType)
    {
        return VisitPropertyType(elementType, [](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            if constexpr (kIsAlternativeOf<T, ArrayValue::Element>) {
                return ArrayValue::Element(std::in_place_type<T>);
            } else {
                return ArrayValue::Element(std::in_place_type<float>, 0.0f);
            }
            });
    }
}

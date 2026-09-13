#include "pch.h"
#include "Reflection/TypeDescriptor.h"

#include "Utility/Logger/Logger.h"

#include <cstring>

namespace CoreEngine::Reflection
{
    size_t SizeOfPropertyType(PropertyType type) noexcept
    {
        switch (type) {
        case PropertyType::Bool:    return sizeof(bool);
        case PropertyType::Int:     return sizeof(int);
        case PropertyType::Float:   return sizeof(float);
        case PropertyType::Vector2: return sizeof(Vector2);
        case PropertyType::Vector3: return sizeof(Vector3);
        case PropertyType::Vector4: return sizeof(Vector4);
        case PropertyType::Color:   return sizeof(Vector4);
        case PropertyType::String:  return sizeof(std::string);
        case PropertyType::ObjectRef: return sizeof(ObjectRefValue);
        }
        return 0;
    }

    const PropertyDescriptor* TypeDescriptor::Find(const char* propertyName) const
    {
        if (!propertyName) {
            return nullptr;
        }
        for (const auto& p : properties) {
            if (p.name == propertyName) {
                return &p;
            }
        }
        return nullptr;
    }

    TypeRegistry& TypeRegistry::Get()
    {
        static TypeRegistry instance;
        return instance;
    }

    void TypeRegistry::Register(const TypeDescriptor* descriptor)
    {
        if (!descriptor || !descriptor->name) {
            return;
        }
        if (Find(descriptor->name)) {
            pendingWarnings_.push_back(
                std::string("型名が重複しています: ") + descriptor->name);
            return;
        }
        types_.push_back(descriptor);
    }

    const TypeDescriptor* TypeRegistry::Find(const char* typeName) const
    {
        if (!typeName) {
            return nullptr;
        }
        for (const TypeDescriptor* d : types_) {
            if (std::strcmp(d->name, typeName) == 0) {
                return d;
            }
        }
        return nullptr;
    }

    void TypeRegistry::FlushPendingWarnings()
    {
        for (const auto& message : pendingWarnings_) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "TypeRegistry: {}", message);
        }
        pendingWarnings_.clear();
    }
}

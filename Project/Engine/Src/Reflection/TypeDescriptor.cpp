#include "pch.h"
#include "Reflection/TypeDescriptor.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstring>

namespace CoreEngine::Reflection
{
    namespace
    {
        /// @brief 版と移行表の食い違いを挙げる
        void CollectMigrationErrors(const TypeDescriptor& type, std::vector<std::string>& errors)
        {
            const std::string name = type.name;
            if (type.version < 1) {
                errors.push_back(name + ": 版は 1 以上にしてください（今は " + std::to_string(type.version) + "）");
                return;
            }

            for (const KeyRename& rename : type.renames) {
                if (rename.version >= 2 && rename.version <= type.version) {
                    continue;
                }
                const std::string label = std::string("改名 \"") + (rename.from ? rename.from : "") +
                    "\" → \"" + (rename.to ? rename.to : "") + "\"";
                if (type.version < 2) {
                    errors.push_back(name + ": " + label + " は、REFLECT_VERSION で版を 2 以上にしてから書いてください");
                } else {
                    errors.push_back(name + ": " + label + " の版 " + std::to_string(rename.version) +
                        " は 2〜" + std::to_string(type.version) + " にしてください");
                }
            }

            if (type.upgrade) {
                return;
            }
            for (uint32_t version = 2; version <= type.version; ++version) {
                const bool renamed = std::any_of(type.renames.begin(), type.renames.end(),
                    [version](const KeyRename& rename) { return rename.version == version; });
                if (!renamed) {
                    errors.push_back(name + ": 版 " + std::to_string(version) +
                        " への移行がありません（REFLECT_RENAMED か REFLECT_UPGRADE で書いてください）");
                }
            }
        }
    }

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
        case PropertyType::AssetRef:  return sizeof(AssetRefValue);
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
        CollectMigrationErrors(*descriptor, pendingErrors_);
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
        for (const auto& message : pendingErrors_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "TypeRegistry: {}", message);
        }
        pendingWarnings_.clear();
        pendingErrors_.clear();
    }
}

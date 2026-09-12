#include "pch.h"
#include "Reflection/PropertySerializer.h"

#include "Reflection/TypeDescriptor.h"

namespace CoreEngine::Reflection
{
    namespace
    {
        json ValueToJson(const PropertyDescriptor& p, const void* value)
        {
            switch (p.type) {
            case PropertyType::Bool:   return *static_cast<const bool*>(value);
            case PropertyType::Int:    return *static_cast<const int*>(value);
            case PropertyType::Float:  return *static_cast<const float*>(value);
            case PropertyType::Vector2: {
                const auto& v = *static_cast<const Vector2*>(value);
                return json::array({ v.x, v.y });
            }
            case PropertyType::Vector3:
                return JsonManager::Vector3ToJson(*static_cast<const Vector3*>(value));
            case PropertyType::Vector4:
            case PropertyType::Color:
                return JsonManager::Vector4ToJson(*static_cast<const Vector4*>(value));
            case PropertyType::String: return *static_cast<const std::string*>(value);
            }
            return {};
        }

        void JsonToValue(const PropertyDescriptor& p, const json& node, void* value)
        {
            switch (p.type) {
            case PropertyType::Bool:
                if (node.is_boolean()) { *static_cast<bool*>(value) = node.get<bool>(); }
                break;
            case PropertyType::Int:
                if (node.is_number()) { *static_cast<int*>(value) = node.get<int>(); }
                break;
            case PropertyType::Float:
                if (node.is_number()) { *static_cast<float*>(value) = node.get<float>(); }
                break;
            case PropertyType::Vector2:
                if (node.is_array() && node.size() >= 2) {
                    auto& v = *static_cast<Vector2*>(value);
                    v.x = node[0].get<float>();
                    v.y = node[1].get<float>();
                }
                break;
            case PropertyType::Vector3:
                if (node.is_array() && node.size() >= 3) {
                    auto& v = *static_cast<Vector3*>(value);
                    v.x = node[0].get<float>();
                    v.y = node[1].get<float>();
                    v.z = node[2].get<float>();
                }
                break;
            case PropertyType::Vector4:
            case PropertyType::Color:
                if (node.is_array() && node.size() >= 4) {
                    auto& v = *static_cast<Vector4*>(value);
                    v.x = node[0].get<float>();
                    v.y = node[1].get<float>();
                    v.z = node[2].get<float>();
                    v.w = node[3].get<float>();
                }
                break;
            case PropertyType::String:
                if (node.is_string()) {
                    *static_cast<std::string*>(value) = node.get<std::string>();
                }
                break;
            }
        }
    }

    void PropertySerializer::Save(const TypeDescriptor& type, const void* instance, json& out)
    {
        for (const auto& p : type.properties) {
            if (!p.IsSaved()) {
                continue;
            }
            const void* value = p.ValuePtr(instance);
            if (value) {
                out[p.name] = ValueToJson(p, value);
            }
        }
    }

    void PropertySerializer::Load(const TypeDescriptor& type, void* instance, const json& in)
    {
        if (!in.is_object()) {
            return;
        }
        for (const auto& p : type.properties) {
            if (!p.IsSaved() || !in.contains(p.name)) {
                continue;
            }
            void* value = p.ValuePtr(instance);
            if (value) {
                JsonToValue(p, in.at(p.name), value);
            }
        }
    }
}

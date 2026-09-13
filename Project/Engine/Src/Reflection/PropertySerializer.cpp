#include "pch.h"
#include "Reflection/PropertySerializer.h"

#include "Reflection/PropertyValue.h"
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
            case PropertyType::ObjectRef: {
                // 何も指していなければ null、指していれば {"ref": ID, "comp": 型名}
                const auto& ref = *static_cast<const ObjectRefValue*>(value);
                if (!ref.objectId.IsValid()) {
                    return nullptr;
                }
                json node = { { "ref", ref.objectId.ToString() } };
                if (!ref.componentType.empty()) {
                    node["comp"] = ref.componentType;
                }
                return node;
            }
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
            case PropertyType::ObjectRef: {
                // null は「何も指さない」。読めない ID は今の値を残す
                auto& ref = *static_cast<ObjectRefValue*>(value);
                if (node.is_null()) {
                    ref = ObjectRefValue{};
                } else if (node.is_object() && node.contains("ref") && node.at("ref").is_string()) {
                    const ObjectId id = ObjectId::FromString(node.at("ref").get<std::string>());
                    if (id.IsValid()) {
                        ref.objectId = id;
                        ref.componentType = (node.contains("comp") && node.at("comp").is_string())
                            ? node.at("comp").get<std::string>()
                            : std::string{};
                    }
                }
                break;
            }
            }
        }
    }

    void PropertySerializer::Save(const TypeDescriptor& type, const void* instance, json& out)
    {
        PropertyValue current;
        for (const auto& p : type.properties) {
            if (!p.IsSaved() || !p.IsValid()) {
                continue;
            }
            current.LoadFrom(p, instance);
            if (const void* value = current.Data(p.type)) {
                out[p.name] = ValueToJson(p, value);
            }
        }
    }

    void PropertySerializer::Load(const TypeDescriptor& type, void* instance, const json& in)
    {
        if (!in.is_object()) {
            return;
        }
        PropertyValue current;
        for (const auto& p : type.properties) {
            if (!p.IsSaved() || !p.IsValid() || !in.contains(p.name)) {
                continue;
            }
            // 現在値を読み出してから JSON を被せる。JSON に足りない要素があっても
            // 既定値ではなく今の値が残る（手書き・旧バージョンの JSON への耐性）
            current.LoadFrom(p, instance);
            void* value = current.Data(p.type);
            if (!value) {
                continue;
            }
            JsonToValue(p, in.at(p.name), value);
            current.StoreTo(p, instance);
        }
    }
}

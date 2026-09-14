#include "pch.h"
#include "Reflection/PropertySerializer.h"

#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
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
            case PropertyType::AssetRef:
                return PropertySerializer::AssetRefToJson(*static_cast<const AssetRefValue*>(value));
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
            case PropertyType::AssetRef:
                // 読めない形なら今の値を残す
                PropertySerializer::JsonToAssetRef(node, *static_cast<AssetRefValue*>(value));
                break;
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

    json PropertySerializer::PropertyToJson(const PropertyDescriptor& property, const void* instance)
    {
        PropertyValue current;
        current.LoadFrom(property, instance);
        const void* value = current.Data(property.type);
        return value ? ValueToJson(property, value) : json{};
    }

    bool PropertySerializer::JsonToProperty(const PropertyDescriptor& property, void* instance, const json& node)
    {
        PropertyValue current;
        current.LoadFrom(property, instance);
        void* value = current.Data(property.type);
        if (!value) {
            return false;
        }
        JsonToValue(property, node, value);
        return current.StoreTo(property, instance);
    }

    json PropertySerializer::AssetRefToJson(const AssetRefValue& value)
    {
        if (value.guid.empty() && value.path.empty()) {
            return nullptr;
        }

        AssetRefValue ref = value;
        if (const AssetInfo* info = ResolveAssetRef(ref)) {
            ref.guid = info->guid;
            ref.path = ToAssetPath(*info);
        }

        json node = json::object();
        if (!ref.guid.empty()) {
            node["guid"] = ref.guid;
        }
        if (!ref.path.empty()) {
            node["path"] = ref.path;
        }
        return node;
    }

    bool PropertySerializer::JsonToAssetRef(const json& node, AssetRefValue& out)
    {
        if (node.is_null()) {
            out = AssetRefValue{};
            return true;
        }
        if (!node.is_object()) {
            return false;
        }

        const bool hasGuid = node.contains("guid") && node.at("guid").is_string();
        const bool hasPath = node.contains("path") && node.at("path").is_string();
        if (!hasGuid && !hasPath) {
            return false;
        }
        out.guid = hasGuid ? node.at("guid").get<std::string>() : std::string{};
        out.path = hasPath ? node.at("path").get<std::string>() : std::string{};
        return true;
    }

    bool PropertySerializer::MigrateParameters(const TypeDescriptor& type, uint32_t savedVersion, json& parameters)
    {
        const uint32_t from = savedVersion < 1 ? 1 : savedVersion;
        if (from > type.version) {
            return false;
        }
        if (from == type.version) {
            return true;
        }
        if (!parameters.is_object()) {
            parameters = json::object();
        }

        for (uint32_t version = from; version < type.version; ++version) {
            const uint32_t next = version + 1;
            for (const KeyRename& rename : type.renames) {
                if (rename.version != next || !rename.from || !rename.to) {
                    continue;
                }
                const auto it = parameters.find(rename.from);
                if (it == parameters.end()) {
                    continue;
                }
                // 新しいキーが既にあれば、そちらの値を残す
                json value = std::move(*it);
                parameters.erase(it);
                if (!parameters.contains(rename.to)) {
                    parameters[rename.to] = std::move(value);
                }
            }
            if (type.upgrade) {
                type.upgrade(version, next, parameters);
                if (!parameters.is_object()) {
                    parameters = json::object();
                }
            }
        }
        return true;
    }

    uint32_t PropertySerializer::ReadVersion(const json& node)
    {
        if (!node.is_number_integer()) {
            return 1;
        }
        const int64_t value = node.get<int64_t>();
        if (value < 1) {
            return 1;
        }
        if (value > static_cast<int64_t>(UINT32_MAX)) {
            return UINT32_MAX;
        }
        return static_cast<uint32_t>(value);
    }

    uint32_t PropertySerializer::ReadComponentVersion(const json& entry)
    {
        if (!entry.is_object()) {
            return 1;
        }
        const auto it = entry.find("version");
        return it != entry.end() ? ReadVersion(*it) : 1;
    }

    void PropertySerializer::WriteComponentVersion(json& entry, uint32_t version)
    {
        if (!entry.is_object()) {
            return;
        }
        if (version > 1) {
            entry["version"] = version;
        } else {
            entry.erase("version");
        }
    }

    bool PropertySerializer::UpgradeComponentEntry(const TypeDescriptor& type, json& entry)
    {
        if (!entry.is_object()) {
            return true;
        }
        const uint32_t saved = ReadComponentVersion(entry);
        if (saved == type.version) {
            return true;
        }

        const auto found = entry.find("parameters");
        json parameters = found != entry.end() ? *found : json::object();
        if (!MigrateParameters(type, saved, parameters)) {
            return false;
        }
        if (parameters.is_object() && !parameters.empty()) {
            entry["parameters"] = std::move(parameters);
        } else {
            entry.erase("parameters");
        }
        WriteComponentVersion(entry, type.version);
        return true;
    }
}

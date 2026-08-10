#include "pch.h"
#include "CVarSerialization.h"
#include "CVar.h"
#include "CVarRegistry.h"
#include "Utility/JsonManager/JsonManager.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

namespace CoreEngine
{
    void CVarSerialization::Save(nlohmann::json& out, std::string_view prefix, bool skipDefaults,
                                 std::string_view excludePrefix)
    {
        for (const ICVar* cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoSave)) {
                continue;
            }
            if (skipDefaults && !cvar->IsModified()) {
                continue;
            }
            if (!excludePrefix.empty()
                && std::string_view(cvar->GetName()).starts_with(excludePrefix)) {
                continue;
            }

            const char* key = cvar->GetName();
            switch (cvar->GetType())
            {
            case CVarType::Bool:
                out[key] = *cvar->AsBool();
                break;
            case CVarType::Int:
                out[key] = *cvar->AsInt();
                break;
            case CVarType::Float:
                out[key] = *cvar->AsFloat();
                break;
            case CVarType::Vector2: {
                const Vector2& v = *cvar->AsVector2();
                out[key] = nlohmann::json::array({ v.x, v.y });
                break;
            }
            case CVarType::Vector3: {
                const Vector3& v = *cvar->AsVector3();
                out[key] = nlohmann::json::array({ v.x, v.y, v.z });
                break;
            }
            case CVarType::Color: {
                const Vector4& v = *cvar->AsColor();
                out[key] = nlohmann::json::array({ v.x, v.y, v.z, v.w });
                break;
            }
            }
        }
    }

    void CVarSerialization::Load(const nlohmann::json& in, std::string_view prefix)
    {
        for (ICVar* cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoSave)) {
                continue;
            }

            const std::string key = cvar->GetName();
            if (!in.contains(key)) {
                continue;  // 保存されていないキーは現在値（コードデフォルト）を維持する
            }

            // 書き込みは SetFromPointer（唯一の変更経路）経由。等価判定と変更通知は
            // Set() 側が行う。保存後に CVar の型を変えた場合は SafeGet が現在値を返すため壊れない
            switch (cvar->GetType())
            {
            case CVarType::Bool: {
                const bool v = JsonManager::SafeGet(in, key, *cvar->AsBool());
                cvar->SetFromPointer(&v);
                break;
            }
            case CVarType::Int: {
                const int v = JsonManager::SafeGet(in, key, *cvar->AsInt());
                cvar->SetFromPointer(&v);
                break;
            }
            case CVarType::Float: {
                const float v = JsonManager::SafeGet(in, key, *cvar->AsFloat());
                cvar->SetFromPointer(&v);
                break;
            }
            case CVarType::Vector2: {
                const auto& node = in[key];
                if (!node.is_array() || node.size() < 2) { break; }
                const Vector2 v{ node[0].get<float>(), node[1].get<float>() };
                cvar->SetFromPointer(&v);
                break;
            }
            case CVarType::Vector3: {
                const auto& node = in[key];
                if (!node.is_array() || node.size() < 3) { break; }
                const Vector3 v{
                    node[0].get<float>(), node[1].get<float>(), node[2].get<float>() };
                cvar->SetFromPointer(&v);
                break;
            }
            case CVarType::Color: {
                const auto& node = in[key];
                if (!node.is_array() || node.size() < 4) { break; }
                const Vector4 v{
                    node[0].get<float>(), node[1].get<float>(),
                    node[2].get<float>(), node[3].get<float>() };
                cvar->SetFromPointer(&v);
                break;
            }
            }
        }
    }
}

#include "pch.h"
#include "CVarSerialization.h"
#include "CVar.h"
#include "CVarRegistry.h"
#include "Utility/JsonManager/JsonManager.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

namespace CoreEngine
{
    void CVarSerialization::Save(nlohmann::json& out, std::string_view prefix, bool skipDefaults)
    {
        for (const ICVar* cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoSave)) {
                continue;
            }
            if (skipDefaults && !cvar->IsModified()) {
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

            // 値を直接書き込み、実際に変化した場合のみ通知する。
            // 保存後に CVar の型を変えた場合は SafeGet が現在値を返すため壊れない
            switch (cvar->GetType())
            {
            case CVarType::Bool: {
                bool* p = cvar->AsBool();
                const bool v = JsonManager::SafeGet(in, key, *p);
                if (*p != v) { *p = v; cvar->NotifyChanged(); }
                break;
            }
            case CVarType::Int: {
                int* p = cvar->AsInt();
                const int v = JsonManager::SafeGet(in, key, *p);
                if (*p != v) { *p = v; cvar->NotifyChanged(); }
                break;
            }
            case CVarType::Float: {
                float* p = cvar->AsFloat();
                const float v = JsonManager::SafeGet(in, key, *p);
                if (*p != v) { *p = v; cvar->NotifyChanged(); }
                break;
            }
            case CVarType::Vector2: {
                Vector2* p = cvar->AsVector2();
                const auto& node = in[key];
                if (!node.is_array() || node.size() < 2) { break; }
                const Vector2 v{ node[0].get<float>(), node[1].get<float>() };
                if (p->x != v.x || p->y != v.y) { *p = v; cvar->NotifyChanged(); }
                break;
            }
            case CVarType::Vector3: {
                Vector3* p = cvar->AsVector3();
                const auto& node = in[key];
                if (!node.is_array() || node.size() < 3) { break; }
                const Vector3 v{
                    node[0].get<float>(), node[1].get<float>(), node[2].get<float>() };
                if (p->x != v.x || p->y != v.y || p->z != v.z) { *p = v; cvar->NotifyChanged(); }
                break;
            }
            case CVarType::Color: {
                Vector4* p = cvar->AsColor();
                const auto& node = in[key];
                if (!node.is_array() || node.size() < 4) { break; }
                const Vector4 v{
                    node[0].get<float>(), node[1].get<float>(),
                    node[2].get<float>(), node[3].get<float>() };
                if (p->x != v.x || p->y != v.y || p->z != v.z || p->w != v.w) { *p = v; cvar->NotifyChanged(); }
                break;
            }
            }
        }
    }
}

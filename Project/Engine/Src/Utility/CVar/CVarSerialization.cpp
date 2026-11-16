#include "pch.h"
#include "CVarSerialization.h"
#include "CVar.h"
#include "CVarRegistry.h"
#include "CVarScope.h"
#include "Utility/JsonManager/JsonManager.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <iterator>
#include <type_traits>

namespace CoreEngine
{
    namespace
    {
        /// @brief Vector の成分（保存する配列の並び）
        template <class T> struct VectorComponents;
        template <> struct VectorComponents<Vector2>
        {
            static constexpr float Vector2::* kMembers[] = { &Vector2::x, &Vector2::y };
        };
        template <> struct VectorComponents<Vector3>
        {
            static constexpr float Vector3::* kMembers[] = { &Vector3::x, &Vector3::y, &Vector3::z };
        };
        template <> struct VectorComponents<Vector4>
        {
            static constexpr float Vector4::* kMembers[] = { &Vector4::x, &Vector4::y, &Vector4::z, &Vector4::w };
        };
    }

    void CVarSerialization::Save(nlohmann::json& out, std::string_view prefix, bool skipDefaults,
                                 std::string_view excludePrefix, CVarScope scope)
    {
        for (const ICVar* cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoSave)) {
                continue;
            }
            if (!CVarScopes::Matches(*cvar, scope)) {
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
            VisitCVarType(cvar->GetType(), [&](auto* tag) {
                using T = std::remove_pointer_t<decltype(tag)>;
                const T& value = cvar->As<T>()->Get();
                if constexpr (std::is_arithmetic_v<T>) {
                    out[key] = value;
                } else {
                    nlohmann::json components = nlohmann::json::array();
                    for (const auto member : VectorComponents<T>::kMembers) {
                        components.push_back(value.*member);
                    }
                    out[key] = std::move(components);
                }
            });
        }
    }

    void CVarSerialization::Load(const nlohmann::json& in, std::string_view prefix,
                                 CVarScope scope)
    {
        for (ICVar* cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoSave)) {
                continue;
            }
            if (!CVarScopes::Matches(*cvar, scope)) {
                continue;
            }

            const std::string key = cvar->GetName();
            if (!in.contains(key)) {
                continue;  // 保存されていないキーは現在値（コードデフォルト）を維持する
            }

            // 等価判定と変更通知は Set() 側が行う。保存後に CVar の型を変えた場合は
            // SafeGet が現在値を返すため壊れない
            VisitCVarType(cvar->GetType(), [&](auto* tag) {
                using T = std::remove_pointer_t<decltype(tag)>;
                CVar<T>& typed = *cvar->As<T>();
                if constexpr (std::is_arithmetic_v<T>) {
                    typed.Set(JsonManager::SafeGet(in, key, typed.Get()));
                } else {
                    const auto& node = in[key];
                    constexpr auto& members = VectorComponents<T>::kMembers;
                    if (!node.is_array() || node.size() < std::size(members)) {
                        return;
                    }
                    T value{};
                    for (size_t i = 0; i < std::size(members); ++i) {
                        value.*members[i] = node[i].get<float>();
                    }
                    typed.Set(value);
                }
            });
        }
    }
}

#include "pch.h"
#include "Scene/PrefabSystem.h"

#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace CoreEngine::PrefabSystem
{
    namespace
    {
        constexpr const char* kComponentsKey = "components";
        constexpr const char* kPrefabKey = "prefab";
        constexpr const char* kOverridesKey = "overrides";
        constexpr const char* kAddedKey = "addedComponents";
        constexpr const char* kRemovedKey = "removedComponents";

        /// @brief 読み込んだプレハブ 1 件分
        struct CachedPrefab
        {
            json components;
            std::filesystem::file_time_type writeTime{};
        };

        /// @brief GUID → 読み込んだプレハブ
        std::unordered_map<std::string, CachedPrefab>& PrefabCache()
        {
            static std::unordered_map<std::string, CachedPrefab> cache;
            return cache;
        }

        /// @brief 書き出したプレハブの内容を控えに入れる
        void StoreInCache(const AssetInfo& info, const json& components)
        {
            std::error_code ec;
            const auto writeTime = std::filesystem::last_write_time(info.fullPath, ec);
            CachedPrefab& entry = PrefabCache()[info.guid];
            entry.components = components;
            entry.writeTime = ec ? std::filesystem::file_time_type{} : writeTime;
        }

        /// @brief `components` 配列を `{"components": …}` の形でファイルへ書く
        bool WritePrefabFile(const std::string& path, const json& components)
        {
            json root = json::object();
            root[kComponentsKey] = components;
            return JsonManager::GetInstance().SaveJson(path, root);
        }

        /// @brief components 配列の 1 要素と、同じ型の中での順番
        struct Slot
        {
            std::string type;
            std::size_t ordinal = 0;
            std::size_t index = 0;  ///< components 配列の添字
        };

        /// @brief components 配列の要素に型ごとの順番を振る（型名の無い要素は飛ばす）
        std::vector<Slot> MakeSlots(const json& components)
        {
            std::vector<Slot> slots;
            if (!components.is_array()) {
                return slots;
            }

            std::unordered_map<std::string, std::size_t> counts;
            for (std::size_t i = 0; i < components.size(); ++i) {
                const json& entry = components[i];
                if (!entry.is_object()) {
                    continue;
                }
                const auto type = entry.find("type");
                if (type == entry.end() || !type->is_string()) {
                    continue;
                }
                std::string name = type->get<std::string>();
                const std::size_t ordinal = counts[name]++;
                slots.push_back(Slot{ std::move(name), ordinal, i });
            }
            return slots;
        }

        const Slot* FindSlot(const std::vector<Slot>& slots, std::string_view type, std::size_t ordinal)
        {
            const auto it = std::find_if(slots.begin(), slots.end(), [&](const Slot& slot) {
                return slot.ordinal == ordinal && slot.type == type;
            });
            return it != slots.end() ? &*it : nullptr;
        }

        /// @brief 型名と順番を `型名` / `型名[n]` の形にする
        std::string SlotName(const std::string& type, std::size_t ordinal)
        {
            return ordinal == 0 ? type : type + "[" + std::to_string(ordinal) + "]";
        }

        /// @brief `型名` / `型名[n]` を型名と順番に分ける
        bool ParseSlotName(std::string_view text, std::string& type, std::size_t& ordinal)
        {
            const std::size_t open = text.find('[');
            if (open == std::string_view::npos) {
                type.assign(text);
                ordinal = 0;
                return !type.empty();
            }
            if (open == 0 || text.size() < open + 3 || text.back() != ']') {
                return false;
            }

            const std::string_view digits = text.substr(open + 1, text.size() - open - 2);
            std::size_t value = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (error != std::errc{} || end != digits.data() + digits.size()) {
                return false;
            }
            type.assign(text.substr(0, open));
            ordinal = value;
            return true;
        }

        bool EnabledOf(const json& entry)
        {
            const auto it = entry.find("enabled");
            return (it != entry.end() && it->is_boolean()) ? it->get<bool>() : true;
        }

        const json& ParametersOf(const json& entry)
        {
            static const json kEmpty = json::object();
            const auto it = entry.find("parameters");
            return (it != entry.end() && it->is_object()) ? *it : kEmpty;
        }
    }

    const json* LoadComponents(const Reflection::AssetRefValue& prefab)
    {
        const AssetInfo* info = ResolveAssetRef(prefab);
        if (!info || info->type != AssetType::Prefab) {
            return nullptr;
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(info->fullPath, ec);

        auto& cache = PrefabCache();
        if (const auto it = cache.find(info->guid);
            it != cache.end() && !ec && it->second.writeTime == writeTime) {
            return &it->second.components;
        }

        const std::string path = ToAssetPath(*info);
        const json root = JsonManager::GetInstance().LoadJson(path);
        const bool readable = root.is_object() && root.contains(kComponentsKey) &&
            root.at(kComponentsKey).is_array();
        if (!readable) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "PrefabSystem: \"{}\" に components 配列がありません", path);
            cache.erase(info->guid);
            return nullptr;
        }

        CachedPrefab& entry = cache[info->guid];
        entry.components = root.at(kComponentsKey);
        entry.writeTime = ec ? std::filesystem::file_time_type{} : writeTime;
        return &entry.components;
    }

    Reflection::AssetRefValue ReadPrefabRef(const json& instance)
    {
        Reflection::AssetRefValue prefab;
        if (instance.is_object() && instance.contains(kPrefabKey)) {
            Reflection::PropertySerializer::JsonToAssetRef(instance.at(kPrefabKey), prefab);
        }
        return prefab;
    }

    bool SameValue(const json& a, const json& b)
    {
        if (a.is_number() && b.is_number()) {
            return static_cast<float>(a.get<double>()) == static_cast<float>(b.get<double>());
        }
        if (a.type() != b.type()) {
            return false;
        }
        if (a.is_array()) {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (!SameValue(a[i], b[i])) {
                    return false;
                }
            }
            return true;
        }
        if (a.is_object()) {
            if (a.size() != b.size()) {
                return false;
            }
            for (auto it = a.begin(); it != a.end(); ++it) {
                const auto other = b.find(it.key());
                if (other == b.end() || !SameValue(*it, *other)) {
                    return false;
                }
            }
            return true;
        }
        return a == b;
    }

    json MakeInstanceJson(const json& full, const json& prefabComponents)
    {
        json instance = full.is_object() ? full : json::object();
        json components = json::array();
        if (const auto it = instance.find(kComponentsKey); it != instance.end()) {
            if (it->is_array()) {
                components = std::move(*it);
            }
            instance.erase(it);
        }

        const std::vector<Slot> prefabSlots = MakeSlots(prefabComponents);
        const std::vector<Slot> ownSlots = MakeSlots(components);

        // プレハブの各コンポーネントと、同じ型・同じ順番のものを突き合わせる
        json overrides = json::object();
        json removed = json::array();
        std::vector<bool> matched(components.size(), false);
        for (const Slot& slot : prefabSlots) {
            const std::string name = SlotName(slot.type, slot.ordinal);
            const Slot* own = FindSlot(ownSlots, slot.type, slot.ordinal);
            if (!own) {
                removed.push_back(name);
                continue;
            }
            matched[own->index] = true;

            const json& base = prefabComponents[slot.index];
            const json& current = components[own->index];
            if (EnabledOf(base) != EnabledOf(current)) {
                overrides[name + "." + kEnabledProperty] = EnabledOf(current);
            }

            const json& baseParameters = ParametersOf(base);
            const json& parameters = ParametersOf(current);
            for (auto it = parameters.begin(); it != parameters.end(); ++it) {
                const auto found = baseParameters.find(it.key());
                if (found == baseParameters.end() || !SameValue(*found, *it)) {
                    overrides[name + "." + it.key()] = *it;
                }
            }
        }

        // プレハブに無いコンポーネントは丸ごと足す
        json added = json::array();
        for (const Slot& slot : ownSlots) {
            if (!matched[slot.index]) {
                added.push_back(components[slot.index]);
            }
        }

        if (!overrides.empty()) {
            instance[kOverridesKey] = std::move(overrides);
        }
        if (!added.empty()) {
            instance[kAddedKey] = std::move(added);
        }
        if (!removed.empty()) {
            instance[kRemovedKey] = std::move(removed);
        }
        return instance;
    }

    json ExpandInstanceJson(const json& instance, const json* prefabComponents,
                            std::vector<std::string>* problems)
    {
        json full = instance.is_object() ? instance : json::object();
        full.erase(kPrefabKey);
        full.erase(kOverridesKey);
        full.erase(kAddedKey);
        full.erase(kRemovedKey);
        if (full.contains(kComponentsKey) || !instance.is_object()) {
            return full;
        }

        const auto report = [problems](std::string message) {
            if (problems) {
                problems->push_back(std::move(message));
            }
        };

        json components = (prefabComponents && prefabComponents->is_array())
            ? *prefabComponents : json::array();
        const std::vector<Slot> slots = MakeSlots(components);

        // 上書きは、プレハブの同じ型・同じ順番のコンポーネントへ書き込む
        if (const auto overrides = instance.find(kOverridesKey);
            overrides != instance.end() && overrides->is_object()) {
            for (auto it = overrides->begin(); it != overrides->end(); ++it) {
                const std::string& key = it.key();
                const std::size_t dot = key.find('.');
                std::string type;
                std::size_t ordinal = 0;
                if (dot == std::string::npos || dot + 1 == key.size() ||
                    !ParseSlotName(std::string_view(key).substr(0, dot), type, ordinal)) {
                    report("上書き \"" + key + "\" のキーを読めないので読み飛ばします");
                    continue;
                }

                const Slot* slot = FindSlot(slots, type, ordinal);
                if (!slot) {
                    if (prefabComponents) {
                        report("プレハブに " + SlotName(type, ordinal) + " が無いので、上書き \"" + key + "\" を読み飛ばします");
                    }
                    continue;
                }

                json& entry = components[slot->index];
                const std::string property = key.substr(dot + 1);
                if (property == kEnabledProperty) {
                    if (it->is_boolean()) {
                        entry["enabled"] = *it;
                    }
                    continue;
                }
                json& parameters = entry["parameters"];
                if (!parameters.is_object()) {
                    parameters = json::object();
                }
                parameters[property] = *it;
            }
        }

        // 取り除く指定は、プレハブでの順番で引いてから後ろから消す
        if (const auto removed = instance.find(kRemovedKey);
            removed != instance.end() && removed->is_array()) {
            std::vector<std::size_t> indices;
            for (const json& item : *removed) {
                std::string type;
                std::size_t ordinal = 0;
                if (!item.is_string() || !ParseSlotName(item.get<std::string>(), type, ordinal)) {
                    report("取り除くコンポーネントの指定 " + item.dump() + " を読めないので読み飛ばします");
                    continue;
                }
                if (const Slot* slot = FindSlot(slots, type, ordinal)) {
                    indices.push_back(slot->index);
                } else if (prefabComponents) {
                    report("プレハブに " + SlotName(type, ordinal) + " が無いので、取り除く指定を読み飛ばします");
                }
            }
            std::sort(indices.begin(), indices.end(), std::greater<>());
            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
            for (const std::size_t index : indices) {
                components.erase(components.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        // 足したコンポーネントは末尾へ並べる
        if (const auto added = instance.find(kAddedKey);
            added != instance.end() && added->is_array()) {
            for (const json& entry : *added) {
                components.push_back(entry);
            }
        }

        full[kComponentsKey] = std::move(components);
        return full;
    }

    std::optional<std::size_t> FindComponentIndex(const json& prefabComponents,
                                                  const GameObject& object, const IComponent& component)
    {
        // オブジェクトの中で、同じ型のコンポーネントの何番目かを数える
        const std::string_view type = component.GetTypeName();
        std::size_t ordinal = 0;
        bool found = false;
        for (const auto& slot : object.GetAllComponents()) {
            if (!slot) {
                continue;
            }
            if (slot.get() == &component) {
                found = true;
                break;
            }
            if (type == slot->GetTypeName()) {
                ++ordinal;
            }
        }
        if (!found) {
            return std::nullopt;
        }

        const std::vector<Slot> slots = MakeSlots(prefabComponents);
        const Slot* slot = FindSlot(slots, type, ordinal);
        return slot ? std::optional<std::size_t>(slot->index) : std::nullopt;
    }

    json MakePrefabComponents(const GameObject& object)
    {
        json components = object.SerializeComponents();
        for (auto& entry : components) {
            if (!entry.is_object() || !entry.contains("parameters") || !entry["parameters"].is_object()) {
                continue;
            }
            for (auto& value : entry["parameters"]) {
                if (value.is_object() && value.contains("ref")) {
                    value = nullptr;
                }
            }
        }
        return components;
    }

    GameObject* Instantiate(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                            const std::string& name)
    {
        const json* components = LoadComponents(prefab);
        const AssetInfo* info = components ? ResolveAssetRef(prefab) : nullptr;
        if (!info) {
            return nullptr;
        }

        auto owned = std::make_unique<GameObject>();
        owned->SetName(name);
        GameObject* object = manager.AddObject(std::move(owned));

        json state = json::object();
        state[kComponentsKey] = *components;
        object->Deserialize(state);
        object->SetPrefab(Reflection::AssetRefValue{ info->guid, ToAssetPath(*info) });
        return object;
    }

    const AssetInfo* CreatePrefab(const std::string& path, const json& components)
    {
        Logger& log = Logger::GetInstance();
        const std::filesystem::path parent = log.Utf8ToPath(path).parent_path();
        if (!parent.empty()) {
            JsonManager::GetInstance().CreateJsonDirectory(log.PathToUtf8(parent));
        }
        if (!WritePrefabFile(path, components)) {
            return nullptr;
        }

        const AssetInfo* info = AssetDatabase::GetInstance().ImportAsset(ProjectPaths::Resolve(path));
        if (!info || info->type != AssetType::Prefab) {
            return nullptr;
        }
        StoreInCache(*info, components);
        return info;
    }

    bool UpdatePrefab(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                      const json& components)
    {
        const json* current = LoadComponents(prefab);
        const AssetInfo* info = current ? ResolveAssetRef(prefab) : nullptr;
        if (!info || !components.is_array()) {
            return false;
        }

        // 書き換える前のプレハブとの差分を控える
        std::vector<std::pair<GameObject*, json>> instances;
        for (const auto& object : manager.GetAllObjects()) {
            if (!object || object->IsMarkedForDestroy() || !object->IsPrefabInstance() ||
                ResolveAssetRef(object->GetPrefab().GetValue()) != info) {
                continue;
            }
            instances.emplace_back(object.get(), MakeInstanceJson(object->Serialize(), *current));
        }

        const json written = components;
        if (!WritePrefabFile(ToAssetPath(*info), written)) {
            return false;
        }
        StoreInCache(*info, written);

        // 新しいプレハブの値に、控えた差分を重ねて戻す
        for (auto& [object, instance] : instances) {
            object->Deserialize(ExpandInstanceJson(instance, &written));
        }
        return true;
    }
}

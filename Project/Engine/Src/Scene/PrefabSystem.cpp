#include "pch.h"
#include "Scene/PrefabSystem.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Reflection/TypeDescriptor.h"
#include "Scene/SceneHierarchy.h"
#include "Utility/Algorithm/FindIfPtr.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace CoreEngine::PrefabSystem
{
    namespace
    {
        constexpr const char* kComponentsKey = "components";
        constexpr const char* kTagKey = "tag";
        constexpr const char* kPrefabKey = "prefab";
        constexpr const char* kOverridesKey = "overrides";
        constexpr const char* kAddedKey = "addedComponents";
        constexpr const char* kRemovedKey = "removedComponents";
        constexpr const char* kVersionsKey = "versions";
        constexpr const char* kChildrenKey = "children";
        constexpr const char* kNodeKey = "node";
        constexpr const char* kNameKey = "name";
        constexpr const char* kActiveKey = "active";

        /// @brief 読み込んだプレハブ 1 件分
        struct CachedPrefab
        {
            PrefabContents contents;
            std::filesystem::file_time_type writeTime{};
            bool upgraded = false;       ///< contents のコンポーネントを型の今の版の形へ書き換え済みか
            bool hasReferences = false;  ///< contents の中に、プレハブの中の番号で指す参照があるか
        };

        /// @brief GUID → 読み込んだプレハブ
        std::unordered_map<std::string, CachedPrefab>& PrefabCache()
        {
            static std::unordered_map<std::string, CachedPrefab> cache;
            return cache;
        }

        /// @brief 中身のどこかに、プレハブの中の番号で指す参照があるか
        bool ContainsReferences(const PrefabContents& contents)
        {
            if (Reflection::PropertySerializer::ContainsObjectRef(contents.components)) {
                return true;
            }
            return std::any_of(contents.children.begin(), contents.children.end(),
                [](const PrefabChild& child) {
                    return Reflection::PropertySerializer::ContainsObjectRef(child.components);
                });
        }

        /// @brief 書き出したプレハブの内容を控えに入れる
        void StoreInCache(const AssetInfo& info, const PrefabContents& contents)
        {
            std::error_code ec;
            const auto writeTime = std::filesystem::last_write_time(info.fullPath, ec);
            CachedPrefab& entry = PrefabCache()[info.guid];
            entry.contents = contents;
            entry.writeTime = ec ? std::filesystem::file_time_type{} : writeTime;
            entry.upgraded = true;
            entry.hasReferences = ContainsReferences(entry.contents);
        }

        /// @brief 中身を `{"tag", "components", "children"}` の形でファイルへ書く
        /// @note `tag` は Untagged のとき、`children` は子が無いとき、子の `active` は true のとき書かない。
        bool WritePrefabFile(const std::string& path, const PrefabContents& contents)
        {
            json root = json::object();
            if (contents.tag != GameObjectTags::kUntagged) {
                root[kTagKey] = contents.tag;
            }
            root[kComponentsKey] = contents.components;
            if (!contents.children.empty()) {
                json children = json::array();
                for (const PrefabChild& child : contents.children) {
                    json entry = json::object();
                    entry[kNodeKey] = child.node;
                    entry[kNameKey] = child.name;
                    if (!child.active) {
                        entry[kActiveKey] = false;
                    }
                    if (child.tag != GameObjectTags::kUntagged) {
                        entry[kTagKey] = child.tag;
                    }
                    entry[kComponentsKey] = child.components;
                    children.push_back(std::move(entry));
                }
                root[kChildrenKey] = std::move(children);
            }
            return JsonManager::GetInstance().SaveJson(path, root);
        }

        /// @brief オブジェクトの JSON のタグ（書いていなければ Untagged）
        std::string TagOf(const json& object)
        {
            const auto found = object.find(kTagKey);
            return (found != object.end() && found->is_string() && !found->get_ref<const std::string&>().empty())
                ? found->get<std::string>() : std::string(GameObjectTags::kUntagged);
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
            return FindIfPtr(slots, [&](const Slot& slot) {
                return slot.ordinal == ordinal && slot.type == type;
            });
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

        /// @brief components 配列の各要素を、型の今の版の形へ書き換える
        /// @param source 警告に出す読み込み元
        void UpgradeComponents(json& components, const std::string& source)
        {
            if (!components.is_array()) {
                return;
            }
            for (json& entry : components) {
                if (!entry.is_object()) {
                    continue;
                }
                const auto type = entry.find("type");
                if (type == entry.end() || !type->is_string()) {
                    continue;
                }
                const std::string typeName = type->get<std::string>();
                const Reflection::TypeDescriptor* descriptor = ComponentFactory::Get().FindDescriptor(typeName);
                const uint32_t saved = Reflection::PropertySerializer::ReadComponentVersion(entry);
                if (!descriptor || saved == descriptor->version) {
                    continue;
                }
                if (!Reflection::PropertySerializer::UpgradeComponentEntry(*descriptor, entry)) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "PrefabSystem: \"{}\" の {} は版 {} で、今の版 {} より新しいので、そのまま使います",
                        source, typeName, saved, descriptor->version);
                }
            }
        }

        /// @brief 控えたプレハブを、まだなら型の今の版の形へ書き換える
        /// @note ファクトリが型名を解決する前は記述子を引けないので、解決した後の読み込みまで書き換えない。
        void EnsureUpgraded(CachedPrefab& prefab, const AssetInfo& info)
        {
            if (prefab.upgraded || !ComponentFactory::Get().IsPrimed()) {
                return;
            }
            const std::string source = ToAssetPath(info);
            UpgradeComponents(prefab.contents.components, source);
            for (PrefabChild& child : prefab.contents.children) {
                UpgradeComponents(child.components, source);
            }
            prefab.upgraded = true;
        }

        /// @brief プレハブのファイルの `children` を読む（読めない要素は警告して飛ばす）
        /// @param path 警告に出す読み込み元
        std::vector<PrefabChild> ReadChildren(const json& root, const std::string& path)
        {
            std::vector<PrefabChild> children;
            const auto found = root.find(kChildrenKey);
            if (found == root.end() || !found->is_array()) {
                return children;
            }

            std::unordered_set<std::string> seen = { kRootNode };
            for (const json& entry : *found) {
                const bool readable = entry.is_object() &&
                    entry.contains(kNodeKey) && entry.at(kNodeKey).is_string() &&
                    entry.contains(kComponentsKey) && entry.at(kComponentsKey).is_array();
                if (!readable) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "PrefabSystem: \"{}\" の子に node か components 配列が無いので読み飛ばします", path);
                    continue;
                }

                PrefabChild child;
                child.node = entry.at(kNodeKey).get<std::string>();
                if (!ObjectId::FromString(child.node).IsValid() || !seen.insert(child.node).second) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "PrefabSystem: \"{}\" の子の node \"{}\" は読めないか重なっているので読み飛ばします",
                        path, child.node);
                    continue;
                }
                if (const auto name = entry.find(kNameKey); name != entry.end() && name->is_string()) {
                    child.name = name->get<std::string>();
                }
                if (const auto active = entry.find(kActiveKey); active != entry.end() && active->is_boolean()) {
                    child.active = active->get<bool>();
                }
                child.tag = TagOf(entry);
                child.components = entry.at(kComponentsKey);
                children.push_back(std::move(child));
            }
            return children;
        }

        /// @brief プレハブを読み、控えの 1 件を返す
        /// @return 見つからない・プレハブでない・読めなければ nullptr
        const CachedPrefab* LoadCached(const Reflection::AssetRefValue& prefab)
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
                EnsureUpgraded(it->second, *info);
                return &it->second;
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
            entry.contents.tag = TagOf(root);
            entry.contents.components = root.at(kComponentsKey);
            entry.contents.children = ReadChildren(root, path);
            entry.hasReferences = ContainsReferences(entry.contents);
            entry.upgraded = false;
            EnsureUpgraded(entry, *info);
            entry.writeTime = ec ? std::filesystem::file_time_type{} : writeTime;
            return &entry;
        }

        /// @brief プレハブの子を番号で探す（無ければ nullptr）
        const PrefabChild* FindChildByNode(const PrefabContents& prefab, std::string_view node)
        {
            return FindIfPtr(prefab.children, [&](const PrefabChild& child) { return child.node == node; });
        }

        /// @brief 上書きを、保存したときの版から型の今の版の形へ書き換える
        /// @param versions インスタンスの `versions`（`型名 → 版`。書いていない型は版 1 として扱う）
        /// @param report 読み飛ばした指定の説明を渡す先
        json UpgradeOverrides(const json& overrides, const json* versions,
                              const std::function<void(std::string)>& report)
        {
            // 有効・無効と読めないキーはそのまま残し、残りをコンポーネントごとの parameters の形にまとめる
            json upgraded = json::object();
            std::map<std::string, json> groups;
            for (auto it = overrides.begin(); it != overrides.end(); ++it) {
                const std::string& key = it.key();
                const std::size_t dot = key.find('.');
                if (dot == std::string::npos || dot + 1 == key.size() ||
                    key.compare(dot + 1, std::string::npos, kEnabledProperty) == 0) {
                    upgraded[key] = *it;
                    continue;
                }
                groups[key.substr(0, dot)][key.substr(dot + 1)] = *it;
            }

            for (auto& [slotName, parameters] : groups) {
                std::string type;
                std::size_t ordinal = 0;
                const Reflection::TypeDescriptor* descriptor = ParseSlotName(slotName, type, ordinal)
                    ? ComponentFactory::Get().FindDescriptor(type) : nullptr;
                if (descriptor) {
                    uint32_t saved = 1;
                    if (versions && versions->is_object()) {
                        if (const auto found = versions->find(type); found != versions->end()) {
                            saved = Reflection::PropertySerializer::ReadVersion(*found);
                        }
                    }
                    if (!Reflection::PropertySerializer::MigrateParameters(*descriptor, saved, parameters)) {
                        report("上書きの " + type + " は版 " + std::to_string(saved) + " で、今の版 " +
                            std::to_string(descriptor->version) + " より新しいので、そのまま使います");
                    }
                }
                for (auto it = parameters.begin(); it != parameters.end(); ++it) {
                    upgraded[slotName + "." + it.key()] = *it;
                }
            }
            return upgraded;
        }
    }

    const PrefabContents* Load(const Reflection::AssetRefValue& prefab)
    {
        const CachedPrefab* const cached = LoadCached(prefab);
        return cached ? &cached->contents : nullptr;
    }

    const PrefabChild* FindChild(const PrefabContents& prefab, std::string_view node)
    {
        return FindChildByNode(prefab, node);
    }

    bool HasReferences(const Reflection::AssetRefValue& prefab)
    {
        const CachedPrefab* const cached = LoadCached(prefab);
        return cached && cached->hasReferences;
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

    bool SameValue(const PrefabContents& a, const PrefabContents& b)
    {
        if (a.tag != b.tag || !SameValue(a.components, b.components) ||
            a.children.size() != b.children.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.children.size(); ++i) {
            const PrefabChild& x = a.children[i];
            const PrefabChild& y = b.children[i];
            if (x.node != y.node || x.name != y.name || x.active != y.active || x.tag != y.tag ||
                !SameValue(x.components, y.components)) {
                return false;
            }
        }
        return true;
    }

    json MakeInstanceJson(const json& full, const PrefabContents& prefab)
    {
        const json& prefabComponents = prefab.components;
        json instance = full.is_object() ? full : json::object();

        // タグはプレハブと違うときだけ書く（Untagged に変えたときも書く）
        const std::string tag = TagOf(instance);
        if (tag == prefab.tag) {
            instance.erase(kTagKey);
        } else {
            instance[kTagKey] = tag;
        }

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
        json versions = json::object();
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
            const std::size_t overrideCount = overrides.size();
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

            // 上書きを書いた型は、保存したときの版を控える
            const uint32_t version = Reflection::PropertySerializer::ReadComponentVersion(current);
            if (overrides.size() != overrideCount && version > 1) {
                versions[slot.type] = version;
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
        if (!versions.empty()) {
            instance[kVersionsKey] = std::move(versions);
        }
        if (!added.empty()) {
            instance[kAddedKey] = std::move(added);
        }
        if (!removed.empty()) {
            instance[kRemovedKey] = std::move(removed);
        }
        return instance;
    }

    json ExpandInstanceJson(const json& instance, const PrefabContents* prefab,
                            std::vector<std::string>* problems)
    {
        json full = instance.is_object() ? instance : json::object();
        full.erase(kPrefabKey);
        full.erase(kPrefabRootKey);
        full.erase(kPrefabNodeKey);
        full.erase(kOverridesKey);
        full.erase(kAddedKey);
        full.erase(kRemovedKey);
        full.erase(kVersionsKey);
        if (full.contains(kComponentsKey) || !instance.is_object()) {
            return full;
        }

        // タグを書いていなければプレハブのタグ
        if (!full.contains(kTagKey) && prefab) {
            full[kTagKey] = prefab->tag;
        }
        const json* prefabComponents = prefab ? &prefab->components : nullptr;

        const auto report = [problems](std::string message) {
            if (problems) {
                problems->push_back(std::move(message));
            }
        };

        json components = (prefabComponents && prefabComponents->is_array())
            ? *prefabComponents : json::array();
        const std::vector<Slot> slots = MakeSlots(components);

        // 上書きは、保存したときの版から今の版の形へ書き換えてから、プレハブの同じ型・同じ順番のコンポーネントへ書き込む
        if (const auto stored = instance.find(kOverridesKey);
            stored != instance.end() && stored->is_object()) {
            const auto versions = instance.find(kVersionsKey);
            const json overrides = UpgradeOverrides(
                *stored, versions != instance.end() ? &*versions : nullptr, report);
            for (auto it = overrides.begin(); it != overrides.end(); ++it) {
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

    PrefabContents MakePrefabContents(const GameObjectManager& manager, const GameObject& root,
                                      const std::unordered_map<ObjectId, std::string>& nodes)
    {
        // シーンの ID → プレハブの中の番号
        NodeIdMap ids;
        ids.emplace(root.GetObjectId().ToString(), kRootNode);
        for (const auto& [id, node] : nodes) {
            ids.emplace(id.ToString(), node);
        }

        PrefabContents contents;
        contents.tag = root.GetTag();
        contents.components = root.SerializeComponents();
        Reflection::PropertySerializer::RemapObjectRefs(contents.components, ids, true);

        for (const GameObject* const child : SceneHierarchy::CollectDescendants(manager, root)) {
            const auto found = nodes.find(child->GetObjectId());
            if (found == nodes.end()) {
                continue;
            }
            PrefabChild entry;
            entry.node = found->second;
            entry.name = child->GetName();
            entry.active = child->IsActive();
            entry.tag = child->GetTag();
            entry.components = child->SerializeComponents();
            Reflection::PropertySerializer::RemapObjectRefs(entry.components, ids, true);
            contents.children.push_back(std::move(entry));
        }
        return contents;
    }

    std::string MakeNode(std::vector<std::string>& used)
    {
        static std::mt19937_64 engine{ std::random_device{}() };
        for (;;) {
            // 未割り当ての 0 と根の番号の 1 は使わない
            const ObjectId id{ engine() };
            if (id.value <= 1) {
                continue;
            }
            std::string node = id.ToString();
            if (std::find(used.begin(), used.end(), node) == used.end()) {
                used.push_back(node);
                return node;
            }
        }
    }

    NodeIdMap CollectInstanceNodes(const GameObjectManager& manager, const GameObject& root)
    {
        NodeIdMap ids;
        const ObjectId rootId = root.GetObjectId();
        ids.emplace(kRootNode, rootId.ToString());
        for (const auto& object : manager.GetAllObjects()) {
            if (object && !object->IsMarkedForDestroy() && object->GetPrefabRootId() == rootId) {
                ids.emplace(object->GetPrefabNode(), object->GetObjectId().ToString());
            }
        }
        return ids;
    }

    InstanceIndex BuildInstanceIndex(const GameObjectManager& manager)
    {
        InstanceIndex index;
        for (const auto& object : manager.GetAllObjects()) {
            if (!object || object->IsMarkedForDestroy() || !object->IsPrefabChild()) {
                continue;
            }
            const ObjectId rootId = object->GetPrefabRootId();
            NodeIdMap& ids = index[rootId];
            if (ids.empty()) {
                ids.emplace(kRootNode, rootId.ToString());
            }
            ids.emplace(object->GetPrefabNode(), object->GetObjectId().ToString());
        }
        return index;
    }

    GameObject* FindInstanceRoot(const GameObjectManager& manager, const GameObject& object)
    {
        if (object.IsPrefabInstance()) {
            return manager.FindObject(object.GetObjectId());
        }
        if (!object.IsPrefabChild()) {
            return nullptr;
        }
        GameObject* const root = manager.FindObject(object.GetPrefabRootId());
        return (root && !root->IsMarkedForDestroy() && root->IsPrefabInstance()) ? root : nullptr;
    }

    std::optional<PrefabContents> ResolveNode(const PrefabContents& prefab, std::string_view node,
                                              const NodeIdMap& ids)
    {
        PrefabContents resolved;
        if (node == kRootNode) {
            resolved.tag = prefab.tag;
            resolved.components = prefab.components;
        } else {
            const PrefabChild* const child = FindChildByNode(prefab, node);
            if (!child) {
                return std::nullopt;
            }
            resolved.tag = child->tag;
            resolved.components = child->components;
        }
        Reflection::PropertySerializer::RemapObjectRefs(resolved.components, ids, true);
        return resolved;
    }

    std::optional<PrefabContents> ResolveBase(const GameObjectManager& manager, const GameObject& object,
                                              const InstanceIndex* index)
    {
        const GameObject* const root = FindInstanceRoot(manager, object);
        const PrefabContents* const prefab = root ? Load(root->GetPrefab().GetValue()) : nullptr;
        if (!prefab) {
            return std::nullopt;
        }

        // 索引に無い根は、子を持たない
        NodeIdMap collected;
        const NodeIdMap* ids = &collected;
        if (!index) {
            collected = CollectInstanceNodes(manager, *root);
        } else if (const auto found = index->find(root->GetObjectId()); found != index->end()) {
            ids = &found->second;
        } else {
            collected.emplace(kRootNode, root->GetObjectId().ToString());
        }

        const std::string_view node = (root == &object) ? std::string_view(kRootNode)
                                                        : std::string_view(object.GetPrefabNode());
        return ResolveNode(*prefab, node, *ids);
    }

    GameObject* Instantiate(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                            const std::string& name, const std::string& serializeKey,
                            std::vector<GameObject*>* outChildren)
    {
        if (outChildren) {
            outChildren->clear();
        }
        const CachedPrefab* const cached = LoadCached(prefab);
        const AssetInfo* info = cached ? ResolveAssetRef(prefab) : nullptr;
        if (!info) {
            return nullptr;
        }
        const PrefabContents& contents = cached->contents;
        const bool hasReferences = cached->hasReferences;

        auto owned = std::make_unique<GameObject>();
        owned->SetSerializeKey(serializeKey);
        owned->SetName(name);
        GameObject* const root = manager.AddObject(std::move(owned));

        json state = json::object();
        state[kTagKey] = contents.tag;
        state[kComponentsKey] = contents.components;
        if (!hasReferences) {
            root->Deserialize(state);
            root->SetPrefab(Reflection::AssetRefValue{ info->guid, ToAssetPath(*info) });
            return root;
        }

        // 先に子の器を全部作って ID を決め、プレハブの中の番号をその ID へ付け替えてから値を流す
        NodeIdMap ids;
        ids.emplace(kRootNode, root->GetObjectId().ToString());
        std::vector<GameObject*> children;
        children.reserve(contents.children.size());
        for (const PrefabChild& child : contents.children) {
            auto ownedChild = std::make_unique<GameObject>();
            ownedChild->SetSerializeKey(root->GetSerializeKey() + "_" + child.name);
            ownedChild->SetName(child.name);
            GameObject* const object = manager.AddObject(std::move(ownedChild));
            ids.emplace(child.node, object->GetObjectId().ToString());
            children.push_back(object);
        }

        Reflection::PropertySerializer::RemapObjectRefs(state[kComponentsKey], ids, true);
        root->Deserialize(state);
        root->SetPrefab(Reflection::AssetRefValue{ info->guid, ToAssetPath(*info) });

        for (std::size_t i = 0; i < children.size(); ++i) {
            const PrefabChild& child = contents.children[i];
            json childState = json::object();
            childState[kActiveKey] = child.active;
            childState[kTagKey] = child.tag;
            childState[kComponentsKey] = child.components;
            Reflection::PropertySerializer::RemapObjectRefs(childState[kComponentsKey], ids, true);
            children[i]->Deserialize(childState);
            children[i]->SetPrefabChild(root->GetObjectId(), child.node);
        }
        manager.InvalidateReferences();

        if (outChildren) {
            *outChildren = std::move(children);
        }
        return root;
    }

    const AssetInfo* CreatePrefab(const std::string& path, const PrefabContents& contents)
    {
        Logger& log = Logger::GetInstance();
        const std::filesystem::path parent = log.Utf8ToPath(path).parent_path();
        if (!parent.empty()) {
            JsonManager::GetInstance().CreateJsonDirectory(log.PathToUtf8(parent));
        }
        if (!WritePrefabFile(path, contents)) {
            return nullptr;
        }

        const AssetInfo* info = AssetDatabase::GetInstance().ImportAsset(ProjectPaths::Resolve(path));
        if (!info || info->type != AssetType::Prefab) {
            return nullptr;
        }
        StoreInCache(*info, contents);
        return info;
    }

    bool UpdatePrefab(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                      const PrefabContents& contents)
    {
        const PrefabContents* current = Load(prefab);
        const AssetInfo* info = current ? ResolveAssetRef(prefab) : nullptr;
        if (!info || !contents.components.is_array()) {
            return false;
        }

        // 書き換える前のプレハブとの差分を、根と子のそれぞれで控える
        struct Pending
        {
            GameObject* object = nullptr;
            std::string node;
            json instance;
            const NodeIdMap* ids = nullptr;
            bool followsName = false;    ///< 名前が書き換える前のプレハブと同じだったか（子だけ）
            bool followsActive = false;  ///< 有効・無効が書き換える前のプレハブと同じだったか（子だけ）
        };
        std::deque<NodeIdMap> maps;
        std::vector<Pending> pending;
        for (const auto& object : manager.GetAllObjects()) {
            if (!object || object->IsMarkedForDestroy() || !object->IsPrefabInstance() ||
                ResolveAssetRef(object->GetPrefab().GetValue()) != info) {
                continue;
            }
            const NodeIdMap& ids = maps.emplace_back(CollectInstanceNodes(manager, *object));
            for (const auto& [node, idText] : ids) {
                GameObject* const target = manager.FindObject(ObjectId::FromString(idText));
                const std::optional<PrefabContents> base = target ? ResolveNode(*current, node, ids) : std::nullopt;
                if (!base) {
                    continue;
                }
                Pending entry;
                entry.object = target;
                entry.node = node;
                entry.instance = MakeInstanceJson(target->Serialize(), *base);
                entry.ids = &ids;
                if (const PrefabChild* const child = FindChildByNode(*current, node)) {
                    entry.followsName = target->GetName() == child->name;
                    entry.followsActive = target->IsActive() == child->active;
                }
                pending.push_back(std::move(entry));
            }
        }

        const PrefabContents written = contents;
        if (!WritePrefabFile(ToAssetPath(*info), written)) {
            return false;
        }
        StoreInCache(*info, written);

        // 新しいプレハブの値に、控えた差分を重ねて戻す
        for (Pending& entry : pending) {
            const std::optional<PrefabContents> base = ResolveNode(written, entry.node, *entry.ids);
            if (!base) {
                continue;
            }
            json state = ExpandInstanceJson(entry.instance, &*base);
            if (const PrefabChild* const child = FindChildByNode(written, entry.node)) {
                if (entry.followsName) {
                    state[kNameKey] = child->name;
                }
                if (entry.followsActive) {
                    state[kActiveKey] = child->active;
                }
            }
            entry.object->Deserialize(state);
        }
        return true;
    }
}

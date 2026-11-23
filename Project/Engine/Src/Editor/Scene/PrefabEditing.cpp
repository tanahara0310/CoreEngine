#include "pch.h"
#include "Editor/Scene/PrefabEditing.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/ObjectEditing.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertyDescriptor.h"
#include "Reflection/PropertySerializer.h"
#include "Scene/PrefabSystem.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CoreEngine::PrefabEditing
{
    namespace
    {
        /// @brief オブジェクト名をファイル名に使える形にする
        std::string MakeFileStem(const std::string& name)
        {
            std::string stem;
            stem.reserve(name.size());
            for (const char c : name) {
                const bool invalid = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
                    c == '\\' || c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 0x20;
                stem.push_back(invalid ? '_' : c);
            }
            while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) {
                stem.pop_back();
            }
            return stem.empty() ? std::string("Prefab") : stem;
        }

        /// @brief フォルダの中で、まだ使われていないプレハブのパスを作る
        std::string MakeUniquePrefabPath(const std::string& directory, const std::string& name)
        {
            const std::string stem = MakeFileStem(name);
            auto& jm = JsonManager::GetInstance();
            for (int index = 0;; ++index) {
                const std::string path = directory + "/" +
                    (index == 0 ? stem : stem + "_" + std::to_string(index)) + ".prefab";
                if (!jm.FileExists(path)) {
                    return path;
                }
            }
        }

        /// @brief オブジェクト 1 体の、プレハブとのつながり
        struct LinkState
        {
            ObjectId id;
            Reflection::AssetRefValue prefab;  ///< 根として元にするプレハブ
            ObjectId prefabRoot;               ///< プレハブの子として属する根の ID
            std::string prefabNode;            ///< プレハブの子としての、プレハブの中の番号
        };

        /// @brief 今のつながりを控える
        LinkState CaptureLink(const GameObject& object)
        {
            LinkState state;
            state.id = object.GetObjectId();
            state.prefab = object.GetPrefab().GetValue();
            state.prefabRoot = object.GetPrefabRootId();
            state.prefabNode = object.GetPrefabNode();
            return state;
        }

        /// @brief 控えたつながりにする
        void ApplyLink(GameObject& object, const LinkState& state)
        {
            object.SetPrefab(state.prefab);
            if (state.prefabRoot.IsValid()) {
                object.SetPrefabChild(state.prefabRoot, state.prefabNode);
            } else {
                object.ClearPrefabChild();
            }
        }

        /// @brief 今のシーンのオブジェクトを、控えたつながりにする
        void ApplyLinksInScene(const std::vector<LinkState>& states)
        {
            for (const LinkState& state : states) {
                if (GameObject* const object = Editor::SceneAccess::FindObject(state.id)) {
                    ApplyLink(*object, state);
                }
            }
        }

        /// @brief オブジェクトとプレハブのつながりを差し替える操作を履歴へ積む
        void PushLinkCommand(std::string label, std::vector<LinkState> before, std::vector<LinkState> after)
        {
            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                std::move(label),
                [before = std::move(before)] { ApplyLinksInScene(before); },
                [after = std::move(after)] { ApplyLinksInScene(after); },
                true, true));
        }

        /// @brief 置いた物の根と、その子の印を持つオブジェクト（根が先頭）
        std::vector<GameObject*> CollectInstanceObjects(const GameObjectManager& manager, GameObject& root)
        {
            std::vector<GameObject*> objects = { &root };
            const ObjectId rootId = root.GetObjectId();
            for (const auto& object : manager.GetAllObjects()) {
                if (object && object.get() != &root && !object->IsMarkedForDestroy() &&
                    object->GetPrefabRootId() == rootId) {
                    objects.push_back(object.get());
                }
            }
            return objects;
        }

        /// @brief オブジェクトの ID と、控えた状態
        using ObjectStates = std::vector<std::pair<ObjectId, json>>;

        /// @brief プレハブの書き換えを履歴へ積む
        /// @param sourceStates 書き戻したオブジェクトの、書き戻す前の状態（戻すときに一緒に戻す）
        void PushUpdateCommand(const Reflection::AssetRefValue& prefab, std::string label,
                               PrefabSystem::PrefabContents before, PrefabSystem::PrefabContents after,
                               ObjectStates sourceStates)
        {
            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                std::move(label),
                [prefab, before = std::move(before), sourceStates = std::move(sourceStates)] {
                    GameObjectManager* const manager = Editor::SceneAccess::Objects();
                    if (!manager) {
                        return;
                    }
                    PrefabSystem::UpdatePrefab(*manager, prefab, before);
                    for (const auto& [id, state] : sourceStates) {
                        if (GameObject* const object = manager->FindObject(id)) {
                            object->Deserialize(state);
                        }
                    }
                },
                [prefab, after = std::move(after)] {
                    if (GameObjectManager* const manager = Editor::SceneAccess::Objects()) {
                        PrefabSystem::UpdatePrefab(*manager, prefab, after);
                    }
                },
                true, true));
        }

        /// @brief components 配列の中で最初の Transform の parameters を探す
        json* FindFirstTransformParameters(json& components)
        {
            for (auto& entry : components) {
                if (!entry.is_object() || entry.value("type", std::string{}) != "Transform") {
                    continue;
                }
                auto parameters = entry.find("parameters");
                return (parameters != entry.end() && parameters->is_object()) ? &*parameters : nullptr;
            }
            return nullptr;
        }

        /// @brief プレハブを書き換えて Undo に積む（書き戻したオブジェクトの書き戻す前の状態も一緒に控える）
        /// @param sources 書き戻したオブジェクト
        bool UpdateAndPush(GameObjectManager& manager, const std::vector<GameObject*>& sources,
                           const Reflection::AssetRefValue& prefab, std::string label,
                           PrefabSystem::PrefabContents before, PrefabSystem::PrefabContents after)
        {
            ObjectStates sourceStates;
            sourceStates.reserve(sources.size());
            for (const GameObject* const source : sources) {
                sourceStates.emplace_back(source->GetObjectId(), source->Serialize());
            }

            if (!PrefabSystem::UpdatePrefab(manager, prefab, after)) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "PrefabEditing: プレハブ \"{}\" へ書き戻せませんでした", prefab.path);
                return false;
            }
            PushUpdateCommand(prefab, std::move(label), std::move(before), std::move(after), std::move(sourceStates));
            return true;
        }

        /// @brief プレハブの中身から、番号の 1 体分（根か子）を探す
        struct NodeEntry
        {
            std::string* tag = nullptr;
            json* components = nullptr;
        };

        NodeEntry FindNodeEntry(PrefabSystem::PrefabContents& contents, std::string_view node)
        {
            if (node == PrefabSystem::kRootNode) {
                return NodeEntry{ &contents.tag, &contents.components };
            }
            for (PrefabSystem::PrefabChild& child : contents.children) {
                if (child.node == node) {
                    return NodeEntry{ &child.tag, &child.components };
                }
            }
            return NodeEntry{};
        }

        /// @brief 置いた物の 1 体が、プレハブの中のどの番号に当たるか（根は `kRootNode`）
        std::string NodeOf(const GameObject& root, const GameObject& object)
        {
            return (&root == &object) ? std::string(PrefabSystem::kRootNode) : object.GetPrefabNode();
        }
    }

    const AssetInfo* CreateFromObject(GameObject& object, const std::string& directory)
    {
        GameObjectManager* const manager = object.GetObjectManager();
        if (!manager) {
            return nullptr;
        }

        // 子孫にプレハブの中の番号を振る
        const std::vector<GameObject*> descendants = ObjectEditing::CollectRestorableDescendants(*manager, object);
        std::unordered_map<ObjectId, std::string> nodes;
        std::vector<std::string> used;
        for (const GameObject* const descendant : descendants) {
            nodes.emplace(descendant->GetObjectId(), PrefabSystem::MakeNode(used));
        }

        const std::string path = MakeUniquePrefabPath(directory, object.GetName());
        const AssetInfo* info = PrefabSystem::CreatePrefab(path,
            PrefabSystem::MakePrefabContents(*manager, object, nodes));
        if (!info) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "PrefabEditing: \"{}\" からプレハブ \"{}\" を作れませんでした", object.GetName(), path);
            return nullptr;
        }

        // 元のオブジェクトを根として、子孫をプレハブの子としてつなぐ（子孫のほかのプレハブとのつながりは外れる）
        const Reflection::AssetRefValue prefab{ info->guid, ToAssetPath(*info) };
        std::vector<LinkState> before;
        std::vector<LinkState> after;
        before.push_back(CaptureLink(object));
        after.push_back(LinkState{ object.GetObjectId(), prefab, ObjectId{}, std::string{} });
        ApplyLink(object, after.back());
        for (GameObject* const descendant : descendants) {
            if (descendant->IsPrefabInstance()) {
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                    "PrefabEditing: \"{}\" は、プレハブ \"{}\" とのつながりを外して \"{}\" の子にしました",
                    descendant->GetName(), descendant->GetPrefab().GetPath(), prefab.path);
            }
            before.push_back(CaptureLink(*descendant));
            after.push_back(LinkState{ descendant->GetObjectId(), Reflection::AssetRefValue{},
                object.GetObjectId(), nodes.at(descendant->GetObjectId()) });
            ApplyLink(*descendant, after.back());
        }
        PushLinkCommand(object.GetName() + " をプレハブにつなぐ", std::move(before), std::move(after));

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "PrefabEditing: \"{}\" と子孫 {} 個からプレハブ \"{}\" を作りました",
            object.GetName(), descendants.size(), prefab.path);
        return info;
    }

    bool ApplyObject(GameObjectManager& manager, GameObject& object)
    {
        GameObject* const root = PrefabSystem::FindInstanceRoot(manager, object);
        const Reflection::AssetRefValue prefab = root ? root->GetPrefab().GetValue() : Reflection::AssetRefValue{};
        const PrefabSystem::PrefabContents* current = PrefabSystem::Load(prefab);
        if (!current) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabEditing: \"{}\" のプレハブ（パス \"{}\"）を読めないので適用できません",
                object.GetName(), prefab.path);
            return false;
        }

        // プレハブにある番号の子だけを書き戻す
        const std::vector<GameObject*> sources = CollectInstanceObjects(manager, *root);
        std::unordered_map<ObjectId, std::string> nodes;
        for (const GameObject* const source : sources) {
            if (source != root && PrefabSystem::FindChild(*current, source->GetPrefabNode())) {
                nodes.emplace(source->GetObjectId(), source->GetPrefabNode());
            }
        }

        PrefabSystem::PrefabContents before = *current;
        PrefabSystem::PrefabContents after = PrefabSystem::MakePrefabContents(manager, *root, nodes);

        // 子の並びと数はプレハブのまま保ち、置いた物に無い子はプレハブの値を残す
        std::vector<PrefabSystem::PrefabChild> children;
        children.reserve(before.children.size());
        for (const PrefabSystem::PrefabChild& base : before.children) {
            const auto found = std::find_if(after.children.begin(), after.children.end(),
                [&base](const PrefabSystem::PrefabChild& child) { return child.node == base.node; });
            children.push_back(found != after.children.end() ? std::move(*found) : base);
        }
        after.children = std::move(children);

        // 最初の Transform の位置と回転は、プレハブの値を残す
        json* baseTransform = FindFirstTransformParameters(before.components);
        json* newTransform = FindFirstTransformParameters(after.components);
        if (baseTransform && newTransform) {
            for (const char* key : { "translate", "rotate" }) {
                if (baseTransform->contains(key)) {
                    (*newTransform)[key] = baseTransform->at(key);
                }
            }
        }

        // 足した子は書き戻さない
        const std::size_t descendantCount = ObjectEditing::CollectRestorableDescendants(manager, *root).size();
        const std::size_t addedCount = descendantCount > nodes.size() ? descendantCount - nodes.size() : 0;
        if (addedCount > 0) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabEditing: \"{}\" に足した子 {} 個は、プレハブへ書き戻しません", root->GetName(), addedCount);
        }

        return UpdateAndPush(manager, sources, prefab, root->GetName() + " をプレハブへ適用",
            std::move(before), std::move(after));
    }

    bool ApplyProperty(GameObjectManager& manager, GameObject& object, IComponent& component,
                       const Reflection::PropertyDescriptor& property)
    {
        GameObject* const root = PrefabSystem::FindInstanceRoot(manager, object);
        const Reflection::AssetRefValue prefab = root ? root->GetPrefab().GetValue() : Reflection::AssetRefValue{};
        const PrefabSystem::PrefabContents* current = PrefabSystem::Load(prefab);

        PrefabSystem::PrefabContents before = current ? *current : PrefabSystem::PrefabContents{};
        PrefabSystem::PrefabContents after = before;
        const NodeEntry entry = current ? FindNodeEntry(after, NodeOf(*root, object)) : NodeEntry{};
        const std::optional<std::size_t> index = entry.components
            ? PrefabSystem::FindComponentIndex(*entry.components, object, component) : std::nullopt;
        void* instance = component.GetReflectionInstance();
        if (!index || !instance) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabEditing: \"{}\" の {} に当たるコンポーネントがプレハブに無いので適用できません",
                object.GetName(), component.GetTypeName());
            return false;
        }

        json& parameters = (*entry.components)[*index]["parameters"];
        if (!parameters.is_object()) {
            parameters = json::object();
        }

        // 値の中の参照は、シーンの ID からプレハブの中の番号へ付け替える（組の外を指すものは null）
        json value = Reflection::PropertySerializer::PropertyToJson(property, instance);
        PrefabSystem::NodeIdMap sceneToNode;
        for (const auto& [node, id] : PrefabSystem::CollectInstanceNodes(manager, *root)) {
            sceneToNode.emplace(id, node);
        }
        Reflection::PropertySerializer::RemapObjectRefs(value, sceneToNode, true);
        parameters[property.name] = std::move(value);

        return UpdateAndPush(manager, { &object }, prefab,
            object.GetName() + " の " + property.displayName + " をプレハブへ適用",
            std::move(before), std::move(after));
    }

    bool ApplyTag(GameObjectManager& manager, GameObject& object)
    {
        GameObject* const root = PrefabSystem::FindInstanceRoot(manager, object);
        const Reflection::AssetRefValue prefab = root ? root->GetPrefab().GetValue() : Reflection::AssetRefValue{};
        const PrefabSystem::PrefabContents* current = PrefabSystem::Load(prefab);

        PrefabSystem::PrefabContents before = current ? *current : PrefabSystem::PrefabContents{};
        PrefabSystem::PrefabContents after = before;
        const NodeEntry entry = current ? FindNodeEntry(after, NodeOf(*root, object)) : NodeEntry{};
        if (!entry.tag) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabEditing: \"{}\" のプレハブ（パス \"{}\"）を読めないので適用できません",
                object.GetName(), prefab.path);
            return false;
        }

        *entry.tag = object.GetTag();
        return UpdateAndPush(manager, { &object }, prefab, object.GetName() + " のタグをプレハブへ適用",
            std::move(before), std::move(after));
    }

    void Unlink(GameObject& object)
    {
        // プレハブの子を選んでいても、根と子の全部のつながりを外す
        GameObjectManager* const manager = object.GetObjectManager();
        GameObject* const root = manager ? PrefabSystem::FindInstanceRoot(*manager, object) : nullptr;
        const std::vector<GameObject*> targets = root
            ? CollectInstanceObjects(*manager, *root) : std::vector<GameObject*>{ &object };

        std::vector<LinkState> before;
        std::vector<LinkState> after;
        for (GameObject* const target : targets) {
            before.push_back(CaptureLink(*target));
            after.push_back(LinkState{ target->GetObjectId(), Reflection::AssetRefValue{}, ObjectId{}, std::string{} });
            ApplyLink(*target, after.back());
        }
        PushLinkCommand((root ? root : &object)->GetName() + " とプレハブのつながりを外す",
            std::move(before), std::move(after));
    }
}

#endif // CORE_EDITOR

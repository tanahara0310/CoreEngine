#include "pch.h"
#include "Editor/Scene/PrefabMode.h"

#ifdef CORE_EDITOR

#include "Camera/CameraManager.h"
#include "Editor/Command/EditorCommand.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Scene/PrefabSystem.h"
#include "Scene/SceneHierarchy.h"
#include "Utility/Logger/Logger.h"

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief プレハブを書き換え、シーンでそのプレハブから置いたオブジェクトにも反映する
        bool WritePrefab(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                         const PrefabSystem::PrefabContents& contents)
        {
            if (PrefabSystem::UpdatePrefab(manager, prefab, contents)) {
                return true;
            }
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "PrefabMode: プレハブ \"{}\" へ書き戻せませんでした", prefab.path);
            return false;
        }
    }

    GameObject* PrefabMode::Open(GameObjectManager& manager, CameraManager* cameras, const AssetInfo& info)
    {
        if (IsOpen()) {
            return nullptr;
        }
        const Reflection::AssetRefValue prefab{ info.guid, ToAssetPath(info) };
        const PrefabSystem::PrefabContents* const contents = PrefabSystem::Load(prefab);
        if (!contents) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabMode: プレハブ \"{}\" を読めないので開けません", prefab.path);
            return nullptr;
        }

        // シーンの履歴は脇へ置き、開いている間の操作は別の履歴に積む
        sceneHistory_ = EditorCommandStack::Get().TakeHistory();

        hasCameraState_ = cameras != nullptr;
        if (cameras) {
            cameraState_ = CameraSceneStateIO::Capture(*cameras);
            usedSceneCamera_ = cameras->IsUsingSceneCamera();
            cameras->SetUseSceneCamera(true);
        }

        // プレハブとつながない根と子の器を先に全部置く（シーンには保存しない）
        auto owned = std::make_unique<GameObject>();
        owned->SetSerializeKey("PrefabMode_" + info.guid);
        owned->SetName(info.name);
        GameObject* const object = manager.AddObject(std::move(owned));

        PrefabSystem::NodeIdMap ids;
        ids.emplace(PrefabSystem::kRootNode, object->GetObjectId().ToString());
        std::unordered_set<ObjectId> shown = { object->GetObjectId() };
        std::vector<GameObject*> children;
        children.reserve(contents->children.size());
        nodes_.clear();
        for (const PrefabSystem::PrefabChild& child : contents->children) {
            auto ownedChild = std::make_unique<GameObject>();
            ownedChild->SetSerializeKey("PrefabMode_" + info.guid + "_" + child.node);
            ownedChild->SetName(child.name);
            GameObject* const added = manager.AddObject(std::move(ownedChild));
            ids.emplace(child.node, added->GetObjectId().ToString());
            nodes_.emplace(added->GetObjectId(), child.node);
            shown.insert(added->GetObjectId());
            children.push_back(added);
        }

        // プレハブの中の番号を置いた器の ID へ付け替えてから値を流す
        json state = json::object();
        state["tag"] = contents->tag;
        state["components"] = contents->components;
        Reflection::PropertySerializer::RemapObjectRefs(state["components"], ids, true);
        object->Deserialize(state);
        object->SetSerializeEnabled(false);
        for (std::size_t i = 0; i < children.size(); ++i) {
            const PrefabSystem::PrefabChild& child = contents->children[i];
            json childState = json::object();
            childState["active"] = child.active;
            childState["tag"] = child.tag;
            childState["components"] = child.components;
            Reflection::PropertySerializer::RemapObjectRefs(childState["components"], ids, true);
            children[i]->Deserialize(childState);
            children[i]->SetSerializeEnabled(false);
        }
        manager.InvalidateReferences();

        objectId_ = object->GetObjectId();
        prefab_ = prefab;
        const std::size_t slash = prefab.path.find_last_of("/\\");
        fileName_ = slash == std::string::npos ? prefab.path : prefab.path.substr(slash + 1);
        opened_ = *contents;
        written_ = PrefabSystem::MakePrefabContents(manager, *object, nodes_);
        edited_ = false;
        detachedWarned_ = false;
        manager.SetIsolatedObjects(std::move(shown));

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "PrefabMode: プレハブ \"{}\" を開きました", prefab.path);
        return object;
    }

    void PrefabMode::Close(GameObjectManager& manager, CameraManager* cameras)
    {
        if (!IsOpen()) {
            return;
        }
        GameObject* const object = GetObject(manager);
        if (object) {
            Flush(manager, *object);
        }

        // 開いている間の履歴を捨て、シーンの履歴へ戻す
        EditorCommandStack& stack = EditorCommandStack::Get();
        (void)stack.TakeHistory();
        stack.RestoreHistory(std::move(sceneHistory_));
        sceneHistory_ = {};

        // 置いた根と子を消す
        for (const auto& [id, node] : nodes_) {
            if (GameObject* const child = manager.FindObject(id)) {
                SceneAccess::Deselect(*child);
                child->Destroy();
            }
        }
        if (object) {
            SceneAccess::Deselect(*object);
            object->Destroy();
        }
        manager.InvalidateReferences();
        manager.SetIsolatedObjects({});
        nodes_.clear();

        // 開いてから閉じるまでの書き換えを、シーンの履歴の 1 件にする
        if (edited_) {
            stack.Push(MakeValueCommand(fileName_ + " を編集", opened_, written_,
                [prefab = prefab_](const PrefabSystem::PrefabContents& value) {
                    if (GameObjectManager* const objects = SceneAccess::Objects()) {
                        WritePrefab(*objects, prefab, value);
                    }
                },
                false, true));
        }

        if (cameras && hasCameraState_) {
            CameraSceneStateIO::Apply(cameraState_, *cameras);
            cameras->SetUseSceneCamera(usedSceneCamera_);
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "PrefabMode: プレハブ \"{}\" を閉じました", prefab_.path);
        objectId_ = {};
        prefab_ = {};
        fileName_.clear();
        opened_ = {};
        written_ = {};
        hasCameraState_ = false;
    }

    bool PrefabMode::Update(GameObjectManager& manager)
    {
        if (!IsOpen()) {
            return true;
        }
        GameObject* const object = GetObject(manager);
        if (!object || object->IsMarkedForDestroy()) {
            return false;
        }
        // 欄やギズモを動かしている途中は書かない（離したときに 1 回だけ書く）
        if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            return true;
        }
        Flush(manager, *object);
        return true;
    }

    GameObject* PrefabMode::GetObject(const GameObjectManager& manager) const
    {
        return IsOpen() ? manager.FindObject(objectId_) : nullptr;
    }

    void PrefabMode::Flush(GameObjectManager& manager, const GameObject& object)
    {
        // 子が組から外れている間は書き戻さない（書くと、その子がプレハブから消える）
        std::size_t attached = 0;
        for (const GameObject* const child : SceneHierarchy::CollectDescendants(manager, object)) {
            attached += nodes_.contains(child->GetObjectId()) ? 1 : 0;
        }
        if (attached != nodes_.size()) {
            if (!detachedWarned_) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "PrefabMode: \"{}\" の子が {} 個、組から外れているので、プレハブへ書き戻しません"
                    "（子を組から外す・消すことはプレハブモードではできません。親を元に戻すか Ctrl+Z で戻します）",
                    fileName_, nodes_.size() - attached);
                detachedWarned_ = true;
            }
            return;
        }
        detachedWarned_ = false;

        PrefabSystem::PrefabContents current = PrefabSystem::MakePrefabContents(manager, object, nodes_);
        if (PrefabSystem::SameValue(current, written_)) {
            return;
        }
        WritePrefab(manager, prefab_, current);
        written_ = std::move(current);
        edited_ = true;
    }
}

#endif // CORE_EDITOR

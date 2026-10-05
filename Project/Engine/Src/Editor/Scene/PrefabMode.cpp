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
#include "Scene/PrefabSystem.h"
#include "Utility/Logger/Logger.h"

#include <memory>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief プレハブを書き換え、シーンでそのプレハブから置いたオブジェクトにも反映する
        bool WritePrefab(GameObjectManager& manager, const Reflection::AssetRefValue& prefab, const json& components)
        {
            if (PrefabSystem::UpdatePrefab(manager, prefab, components)) {
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
        const json* const components = PrefabSystem::LoadComponents(prefab);
        if (!components) {
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

        // プレハブとつながない 1 体を置く（シーンには保存しない）
        auto owned = std::make_unique<GameObject>();
        owned->SetSerializeKey("PrefabMode_" + info.guid);
        owned->SetName(info.name);
        GameObject* const object = manager.AddObject(std::move(owned));
        json state = json::object();
        state["components"] = *components;
        object->Deserialize(state);
        object->SetSerializeEnabled(false);

        objectId_ = object->GetObjectId();
        prefab_ = prefab;
        const std::size_t slash = prefab.path.find_last_of("/\\");
        fileName_ = slash == std::string::npos ? prefab.path : prefab.path.substr(slash + 1);
        opened_ = *components;
        written_ = PrefabSystem::MakePrefabComponents(*object);
        edited_ = false;
        manager.SetIsolatedObject(objectId_);

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

        if (object) {
            SceneAccess::Deselect(*object);
            object->Destroy();
            manager.InvalidateReferences();
        }
        manager.SetIsolatedObject(ObjectId{});

        // 開いてから閉じるまでの書き換えを、シーンの履歴の 1 件にする
        if (edited_) {
            stack.Push(std::make_unique<FunctionCommand>(
                fileName_ + " を編集",
                [prefab = prefab_, before = opened_] {
                    if (GameObjectManager* const objects = SceneAccess::Objects()) {
                        WritePrefab(*objects, prefab, before);
                    }
                },
                [prefab = prefab_, after = written_] {
                    if (GameObjectManager* const objects = SceneAccess::Objects()) {
                        WritePrefab(*objects, prefab, after);
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
        opened_ = json{};
        written_ = json{};
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
        json current = PrefabSystem::MakePrefabComponents(object);
        if (PrefabSystem::SameValue(current, written_)) {
            return;
        }
        WritePrefab(manager, prefab_, current);
        written_ = std::move(current);
        edited_ = true;
    }
}

#endif // CORE_EDITOR

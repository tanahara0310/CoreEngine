#include "pch.h"
#include "Editor/Scene/PrefabEditing.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertyDescriptor.h"
#include "Reflection/PropertySerializer.h"
#include "Scene/PrefabSystem.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

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

        /// @brief オブジェクトのプレハブへの参照を差し替える操作を履歴へ積む
        void PushLinkCommand(GameObject& object, std::string label,
                             Reflection::AssetRefValue before, Reflection::AssetRefValue after)
        {
            if (!object.GetObjectManager()) {
                return;
            }
            const ObjectId id = object.GetObjectId();
            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                std::move(label),
                [id, before] {
                    if (GameObject* target = Editor::SceneAccess::FindObject(id)) {
                        target->SetPrefab(before);
                    }
                },
                [id, after] {
                    if (GameObject* target = Editor::SceneAccess::FindObject(id)) {
                        target->SetPrefab(after);
                    }
                },
                true, true));
        }

        /// @brief プレハブの書き換えを履歴へ積む
        /// @param sourceState 書き戻したオブジェクトの、書き戻す前の状態（戻すときに一緒に戻す）
        void PushUpdateCommand(const GameObject& source, const Reflection::AssetRefValue& prefab,
                               std::string label, json before, json after, json sourceState)
        {
            const ObjectId id = source.GetObjectId();
            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                std::move(label),
                [prefab, before, id, sourceState] {
                    GameObjectManager* const manager = Editor::SceneAccess::Objects();
                    if (!manager) {
                        return;
                    }
                    PrefabSystem::UpdatePrefab(*manager, prefab, before);
                    if (GameObject* object = manager->FindObject(id)) {
                        object->Deserialize(sourceState);
                    }
                },
                [prefab, after] {
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
    }

    const AssetInfo* CreateFromObject(GameObject& object, const std::string& directory)
    {
        const std::string path = MakeUniquePrefabPath(directory, object.GetName());
        const AssetInfo* info = PrefabSystem::CreatePrefab(path, PrefabSystem::MakePrefabComponents(object));
        if (!info) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "PrefabEditing: \"{}\" からプレハブ \"{}\" を作れませんでした", object.GetName(), path);
            return nullptr;
        }

        const Reflection::AssetRefValue before = object.GetPrefab().GetValue();
        const Reflection::AssetRefValue after{ info->guid, ToAssetPath(*info) };
        object.SetPrefab(after);
        PushLinkCommand(object, object.GetName() + " をプレハブにつなぐ", before, after);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "PrefabEditing: \"{}\" からプレハブ \"{}\" を作りました", object.GetName(), after.path);
        return info;
    }

    bool ApplyObject(GameObjectManager& manager, GameObject& object)
    {
        const Reflection::AssetRefValue prefab = object.GetPrefab().GetValue();
        const json* current = PrefabSystem::LoadComponents(prefab);
        if (!current) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabEditing: \"{}\" のプレハブ（パス \"{}\"）を読めないので適用できません",
                object.GetName(), prefab.path);
            return false;
        }

        json before = *current;
        json after = PrefabSystem::MakePrefabComponents(object);

        // 最初の Transform の位置と回転は、プレハブの値を残す
        json* baseTransform = FindFirstTransformParameters(before);
        json* newTransform = FindFirstTransformParameters(after);
        if (baseTransform && newTransform) {
            for (const char* key : { "translate", "rotate" }) {
                if (baseTransform->contains(key)) {
                    (*newTransform)[key] = baseTransform->at(key);
                }
            }
        }

        json sourceState = object.Serialize();
        if (!PrefabSystem::UpdatePrefab(manager, prefab, after)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "PrefabEditing: プレハブ \"{}\" へ書き戻せませんでした", prefab.path);
            return false;
        }
        PushUpdateCommand(object, prefab, object.GetName() + " をプレハブへ適用",
            std::move(before), std::move(after), std::move(sourceState));
        return true;
    }

    bool ApplyProperty(GameObjectManager& manager, GameObject& object, IComponent& component,
                       const Reflection::PropertyDescriptor& property)
    {
        const Reflection::AssetRefValue prefab = object.GetPrefab().GetValue();
        const json* current = PrefabSystem::LoadComponents(prefab);
        const std::optional<std::size_t> index = current
            ? PrefabSystem::FindComponentIndex(*current, object, component) : std::nullopt;
        void* instance = component.GetReflectionInstance();
        if (!index || !instance) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PrefabEditing: \"{}\" の {} に当たるコンポーネントがプレハブに無いので適用できません",
                object.GetName(), component.GetTypeName());
            return false;
        }

        json before = *current;
        json after = before;
        json& parameters = after[*index]["parameters"];
        if (!parameters.is_object()) {
            parameters = json::object();
        }
        parameters[property.name] = Reflection::PropertySerializer::PropertyToJson(property, instance);

        json sourceState = object.Serialize();
        if (!PrefabSystem::UpdatePrefab(manager, prefab, after)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "PrefabEditing: プレハブ \"{}\" へ書き戻せませんでした", prefab.path);
            return false;
        }
        PushUpdateCommand(object, prefab,
            object.GetName() + " の " + property.displayName + " をプレハブへ適用",
            std::move(before), std::move(after), std::move(sourceState));
        return true;
    }

    void Unlink(GameObject& object)
    {
        const Reflection::AssetRefValue before = object.GetPrefab().GetValue();
        object.SetPrefab(Reflection::AssetRefValue{});
        PushLinkCommand(object, object.GetName() + " とプレハブのつながりを外す",
            before, Reflection::AssetRefValue{});
    }
}

#endif // CORE_EDITOR

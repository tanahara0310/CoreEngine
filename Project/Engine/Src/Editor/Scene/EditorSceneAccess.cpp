#include "pch.h"
#include "Editor/Scene/EditorSceneAccess.h"

#ifdef CORE_EDITOR

#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Scene/SceneManager.h"
#include "Utility/Debug/GameDebugUI.h"

namespace CoreEngine::Editor
{
    namespace
    {
        EngineSystem* sEngine = nullptr;
    }

    void SceneAccess::Bind(EngineSystem* engine)
    {
        sEngine = engine;
    }

    GameObjectManager* SceneAccess::Objects()
    {
        SceneManager* const scenes = sEngine ? sEngine->GetSceneManager() : nullptr;
        return scenes ? scenes->GetCurrentGameObjectManager() : nullptr;
    }

    SceneDebugEditor* SceneAccess::SceneEditor()
    {
        DebugSubsystem* const debug = sEngine ? sEngine->GetDebugSubsystem() : nullptr;
        GameDebugUI* const ui = debug ? debug->GetGameDebugUI() : nullptr;
        return ui ? ui->GetSceneDebugEditor() : nullptr;
    }

    GameObject* SceneAccess::FindObject(ObjectId id)
    {
        GameObjectManager* const objects = Objects();
        return (objects && id.IsValid()) ? objects->FindObject(id) : nullptr;
    }

    void SceneAccess::Deselect(const GameObject& object)
    {
        if (SceneDebugEditor* const editor = SceneEditor()) {
            editor->Deselect(object);
        }
    }

    ComponentHandle ComponentHandle::Of(const IComponent& component)
    {
        ComponentHandle handle;
        handle.type = component.GetTypeName();
        const GameObject* const owner = component.GetOwner();
        if (!owner) {
            return handle;
        }

        handle.object = owner->GetObjectId();
        for (const auto& slot : owner->GetAllComponents()) {
            if (!slot) {
                continue;
            }
            if (slot.get() == &component) {
                break;
            }
            if (handle.type == slot->GetTypeName()) {
                ++handle.ordinal;
            }
        }
        return handle;
    }

    IComponent* ComponentHandle::Resolve() const
    {
        GameObject* const owner = SceneAccess::FindObject(object);
        if (!owner) {
            return nullptr;
        }

        std::size_t index = 0;
        for (const auto& slot : owner->GetAllComponents()) {
            if (!slot || type != slot->GetTypeName()) {
                continue;
            }
            if (index++ == ordinal) {
                return slot.get();
            }
        }
        return nullptr;
    }
}

#endif // CORE_EDITOR

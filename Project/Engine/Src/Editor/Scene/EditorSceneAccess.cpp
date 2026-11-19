#include "pch.h"
#include "Editor/Scene/EditorSceneAccess.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Scene/SceneManager.h"
#include "Editor/ImGui/GameDebugUI.h"
#include "UI/RectTransformComponent.h"
#include "UI/UILayoutPass.h"

namespace CoreEngine::Editor
{
    namespace
    {
        EngineSystem* sEngine = nullptr;

        /// @brief 並べる処理が書き換えた UI トランスフォームを、直前の操作を取り消すときに書き換える前へ戻す
        void KeepLayoutRectForUndo(RectTransformComponent& rect, const UILayoutPass::RectState& before)
        {
            EditorCommandStack& stack = EditorCommandStack::Get();
            if (!stack.CanAttachToLast(&rect)) {
                return;
            }
            stack.AttachToLast(&rect, [handle = ComponentHandle::Of(rect), before] {
                if (auto* const target = dynamic_cast<RectTransformComponent*>(handle.Resolve())) {
                    before.ApplyTo(*target);
                }
            });
        }
    }

    void SceneAccess::Bind(EngineSystem* engine)
    {
        sEngine = engine;
    }

    EngineSystem* SceneAccess::Engine()
    {
        return sEngine;
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

    void SceneAccess::KeepLayoutChangesInUndo(bool enabled)
    {
        UILayoutPass::SetRectChangedCallback(enabled ? &KeepLayoutRectForUndo : nullptr);
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

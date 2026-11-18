#include "pch.h"
#include "AnimatorLiveView.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"

#include "Animation/AnimatorRuntime.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/Component/Animation/AnimatorComponent.h"
#include "GameObject/GameObject.h"

namespace CoreEngine::Editor
{
    AnimatorLiveView AnimatorLiveView::Find(const AnimatorDocument& document)
    {
        AnimatorLiveView live;
        if (!PlaybackStateManager::GetInstance().IsInPlayMode()) {
            return live;
        }
        SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor();
        GameObject* const object = sceneEditor ? sceneEditor->GetSelectedObject() : nullptr;
        AnimatorComponent* const animator = object ? object->GetComponent<AnimatorComponent>() : nullptr;
        if (!animator || !animator->GetController().IsSet()) {
            return live;
        }
        AnimatorControllerLibrary& library = AnimatorControllerLibrary::Get();
        const std::shared_ptr<const AnimatorControllerLibrary::Entry> mine = library.Load(document.assetPath);
        const std::shared_ptr<const AnimatorControllerLibrary::Entry> theirs = library.Load(animator->GetController().GetPath());
        if (!mine || mine != theirs) {
            return live;
        }
        AnimatorRuntime& runtime = animator->GetRuntime();
        const AnimatorControllerData* const controller = runtime.GetController();
        if (!controller || controller->layers.empty()) {
            return live;
        }
        live.animator = animator;
        live.runtime = &runtime;
        live.objectName = object->GetName();
        live.layer = runtime.FindLayer(document.Layer().name);
        if (live.layer < 0) {
            return live;
        }
        live.currentState = runtime.GetLayerStateName(live.layer);
        live.normalizedTime = runtime.GetLayerNormalizedTime(live.layer);
        live.inTransition = runtime.IsLayerInTransition(live.layer);
        live.transitionProgress = runtime.GetLayerTransitionProgress(live.layer);
        const AnimatorLayer& layer = controller->layers[static_cast<size_t>(live.layer)];
        const int source = runtime.GetLayerTransitionSourceIndex(live.layer);
        if (source >= 0 && source < static_cast<int>(layer.states.size())) {
            live.sourceState = layer.states[static_cast<size_t>(source)].name;
        }
        const int table = runtime.GetLayerActiveTransitionIndex(live.layer);
        if (table >= 0 && table < static_cast<int>(layer.transitions.size())) {
            live.transitionFrom = layer.transitions[static_cast<size_t>(table)].from;
            live.transitionTo = layer.transitions[static_cast<size_t>(table)].to;
        }
        return live;
    }
}

#endif // CORE_EDITOR

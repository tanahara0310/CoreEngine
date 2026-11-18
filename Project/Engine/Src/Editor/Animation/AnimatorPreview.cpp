#include "pch.h"
#include "AnimatorPreview.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"

#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/Component/Animation/AnimatorComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Model/ModelManager.h"
#include "Utility/FrameRate/Time.h"

#include <cmath>
#include <memory>
#include <vector>

namespace CoreEngine::Editor
{
    std::string AnimatorPreview::PreviewStateOf(const AnimatorDocument& document)
    {
        if (document.data.layers.empty()) {
            return {};
        }
        const AnimatorLayer& layer = document.data.layers.front();
        if (document.selection == AnimatorSelection::State && document.selected >= 0 &&
            document.selected < static_cast<int>(layer.states.size())) {
            return layer.states[static_cast<size_t>(document.selected)].name;
        }
        if (layer.FindState(layer.defaultState)) {
            return layer.defaultState;
        }
        return layer.states.empty() ? std::string{} : layer.states.front().name;
    }

    bool AnimatorPreview::UsesDocument(const AnimatorComponent& animator, const AnimatorDocument& document)
    {
        if (!animator.GetController().IsSet()) {
            return false;
        }
        AnimatorControllerLibrary& library = AnimatorControllerLibrary::Get();
        const std::shared_ptr<const AnimatorControllerLibrary::Entry> mine = library.Load(document.assetPath);
        const std::shared_ptr<const AnimatorControllerLibrary::Entry> theirs = library.Load(animator.GetController().GetPath());
        return mine && mine == theirs;
    }

    void AnimatorPreview::Update(AnimatorDocument& document)
    {
        document.previewObject.clear();
        if (!document.previewEnabled || PlaybackStateManager::GetInstance().IsInPlayMode()) {
            Stop();
            return;
        }
        SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor();
        GameObject* const object = sceneEditor ? sceneEditor->GetSelectedObject() : nullptr;
        AnimatorComponent* const animator = object ? object->GetComponent<AnimatorComponent>() : nullptr;
        if (!animator || !UsesDocument(*animator, document)) {
            Stop();
            return;
        }
        if (!(object->GetObjectId() == target_)) {
            Stop();
            target_ = object->GetObjectId();
        }
        const Skeleton* const skeleton = animator->GetSkeleton();
        const std::string stateName = PreviewStateOf(document);
        if (!skeleton || stateName.empty()) {
            animator->ClearPreviewPose();
            return;
        }

        const std::string text = AnimatorDocument::ToText(document.data);
        if (text != builtText_ || skeleton != builtSkeleton_) {
            Rebuild(document, *skeleton);
            builtText_ = text;
            builtSkeleton_ = skeleton;
        }
        for (const AnimatorParameter& parameter : document.data.parameters) {
            if (parameter.type == AnimatorParameterType::Float) {
                runtime_.SetFloat(parameter.name, document.PreviewFloat(parameter.name));
            }
        }

        if (document.previewPlaying) {
            const AnimatorState* const state = document.data.layers.front().FindState(stateName);
            const float duration = runtime_.GetStateDuration(stateName);
            if (state && duration > 0.0f) {
                document.previewTime += Time::UnscaledDeltaTime() * state->speed / duration;
                document.previewTime -= std::floor(document.previewTime);
            }
        }
        if (runtime_.EvaluateState(stateName, document.previewTime) &&
            animator->ShowPreviewPose(runtime_.GetSkeletonSpaceMatrices())) {
            document.previewObject = object->GetName();
        }
    }

    void AnimatorPreview::Stop()
    {
        if (!target_.IsValid()) {
            return;
        }
        if (GameObject* const object = SceneAccess::FindObject(target_)) {
            if (AnimatorComponent* const animator = object->GetComponent<AnimatorComponent>()) {
                animator->ClearPreviewPose();
            }
        }
        target_ = ObjectId{};
    }

    void AnimatorPreview::Rebuild(const AnimatorDocument& document, const Skeleton& skeleton)
    {
        auto data = std::make_shared<AnimatorControllerData>(document.data);
        std::vector<std::shared_ptr<const Animation>> clips;
        clips.reserve(data->clips.size());
        for (const AnimatorClip& clip : data->clips) {
            clips.push_back(document.models && !clip.file.empty() ? document.models->AcquireClip(clip.file, clip.source) : nullptr);
        }
        runtime_.Build(std::move(data), std::move(clips), &skeleton, "シーンで見る");
    }
}

#endif // CORE_EDITOR

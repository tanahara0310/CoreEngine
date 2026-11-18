#include "pch.h"
#include "AnimatorPreview.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"

#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/Component/Animation/AnimatorComponent.h"
#include "GameObject/Component/Render/SpriteAnimatorComponent.h"
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
        const AnimatorLayer& layer = document.Layer();
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
        const SpriteAnimatorComponent* const sprite = animator->GetDrivenSprite();
        const std::string stateName = PreviewStateOf(document);
        if ((!skeleton && !sprite) || stateName.empty()) {
            animator->ClearPreviewPose();
            return;
        }

        runtime_.SetRootMotionEnabled(animator->GetApplyRootMotion());
        runtime_.SetBlendingEnabled(!sprite);
        const std::string text = AnimatorDocument::ToText(document.data) + (sprite ? SpriteClipsKey(*sprite) : std::string{});
        if (text != builtText_ || skeleton != builtSkeleton_ || sprite != builtSprite_) {
            Rebuild(document, skeleton, sprite);
            builtText_ = text;
            builtSkeleton_ = skeleton;
            builtSprite_ = sprite;
        }
        for (const AnimatorParameter& parameter : document.data.parameters) {
            if (parameter.type == AnimatorParameterType::Float) {
                runtime_.SetFloat(parameter.name, document.PreviewFloat(parameter.name));
            }
        }

        if (document.previewPlaying) {
            const AnimatorState* const state = document.Layer().FindState(stateName);
            const float duration = runtime_.GetLayerStateDuration(document.activeLayer, stateName);
            if (state && duration > 0.0f) {
                document.previewTime += Time::UnscaledDeltaTime() * state->speed / duration;
                document.previewTime -= std::floor(document.previewTime);
            }
        }
        if (sprite) {
            const AnimatorRuntime::ClipSample sample = runtime_.StateClipAt(document.activeLayer, stateName, document.previewTime);
            const std::vector<AnimatorClip>& clips = document.data.clips;
            if (sample.clip >= 0 && sample.clip < static_cast<int>(clips.size()) && clips[static_cast<size_t>(sample.clip)].IsSprite() &&
                animator->ShowPreviewSprite(clips[static_cast<size_t>(sample.clip)].sprite, sample.time)) {
                document.previewObject = object->GetName();
            }
            return;
        }
        if (runtime_.EvaluateLayerState(document.activeLayer, stateName, document.previewTime) &&
            animator->ShowPreviewPose(runtime_.GetSkeletonSpaceMatrices())) {
            document.previewObject = object->GetName();
        }
    }

    std::string AnimatorPreview::SpriteClipsKey(const SpriteAnimatorComponent& sprite)
    {
        std::string key;
        for (const SpriteAnimationClip& clip : sprite.GetClips()) {
            key += clip.name + ":" + std::to_string(SpriteAnimatorComponent::ClipDuration(clip)) + ";";
        }
        return key;
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

    void AnimatorPreview::Rebuild(const AnimatorDocument& document, const Skeleton* skeleton, const SpriteAnimatorComponent* sprite)
    {
        auto data = std::make_shared<AnimatorControllerData>(document.data);
        std::vector<std::shared_ptr<const Animation>> clips = AnimatorComponent::AcquireClips(*data, document.models, sprite);
        runtime_.Build(std::move(data), std::move(clips), sprite ? &AnimatorRuntime::NoBones() : skeleton, "シーンで見る");
    }
}

#endif // CORE_EDITOR

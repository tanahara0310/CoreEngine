#include "pch.h"
#include "AnimatorComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/ComponentService.h"
#include "GameObject/GameObject.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string_view>

REFLECT_DEFINE_BEGIN(CoreEngine::AnimatorComponent, "アニメーション")
    REFLECT_PARTIAL()
    REFLECT_JSON(SaveClipsToJson, LoadClipsFromJson)
    REFLECT_ACCESSOR("model", "モデル", GetModelPath, SetModelPath,
        p.tooltip = "骨入りのモデル（空なら同じオブジェクトのメッシュ描画のモデルを使う）")
    REFLECT_ASSET_REF(controller_, "設定")
    REFLECT_PROPERTY(speed_, "速さ", p.range = Range(0.0f, 4.0f, 0.01f), p.tooltip = "全体の速さの倍率。1 で各状態の速さどおり")
    REFLECT_READONLY_ACCESSOR("currentState", "今の状態", GetCurrentState)
    REFLECT_READONLY_ACCESSOR("normalizedTime", "進み", GetNormalizedTime)
    REFLECT_READONLY_ACCESSOR("isInTransition", "遷移の途中", IsInTransition)
    REFLECT_METHOD("SetFloat", "数のパラメータを書く", SetFloat)
    REFLECT_METHOD("GetFloat", "数のパラメータを読む", GetFloat)
    REFLECT_METHOD("SetInt", "整数のパラメータを書く", SetInt)
    REFLECT_METHOD("GetInt", "整数のパラメータを読む", GetInt)
    REFLECT_METHOD("SetBool", "真偽のパラメータを書く", SetBool)
    REFLECT_METHOD("GetBool", "真偽のパラメータを読む", GetBool)
    REFLECT_METHOD("SetTrigger", "トリガーを立てる", SetTrigger)
    REFLECT_METHOD("ResetTrigger", "トリガーを下ろす", ResetTrigger)
    REFLECT_METHOD("Play", "状態をすぐ切り替える", Play)
    REFLECT_METHOD("CrossFade", "混ぜながら状態を切り替える", CrossFade)
    REFLECT_METHOD("IsInState", "今の状態か", IsInState)
    REFLECT_METHOD("Switch", "クリップの切り替え", Switch)
    REFLECT_METHOD("SwitchWithBlend", "クリップの切り替え（ブレンド）", SwitchWithBlend)
    REFLECT_METHOD("GetCurrentClipName", "再生中のクリップ名", GetCurrentClipName)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::AnimatorComponent)
COMPONENT_REGISTER(CoreEngine::AnimatorComponent)

namespace CoreEngine
{
    namespace {
        /// 骨（親→子）を結ぶ線の色
        constexpr Vector3 kBoneColor = { 0.2f, 1.0f, 0.3f };
        /// ジョイント位置を示すマーカーの色
        constexpr Vector3 kJointColor = { 1.0f, 0.9f, 0.2f };
        /// ジョイントマーカーの大きさ [m]
        constexpr float kJointMarkerSize = 0.02f;
        /// 骨はメッシュ内部にあるので深度テストを切って手前に描く（LineManager の depthTest 引数）
        constexpr bool kDrawThroughMesh = false;
    }

    void AnimatorComponent::SetModelPath(const std::string& modelPath)
    {
        modelPath_ = modelPath;
        clipController_.reset();
    }

    void AnimatorComponent::SetController(std::string_view pathOrName)
    {
        controller_.SetPath(pathOrName);
        controllerEntry_.reset();
        bound_ = false;
        if (awoken_) {
            ApplyToRenderer();
        }
    }

    void AnimatorComponent::SaveClipsToJson(json& parameters) const
    {
        if (clips_.empty()) {
            return;
        }
        json clips = json::array();
        for (const AnimationClipDesc& clip : clips_) {
            json entry = { { "name", clip.name } };
            if (!clip.sourceName.empty()) { entry["sourceName"] = clip.sourceName; }
            if (!clip.file.empty()) { entry["file"] = clip.file; }
            clips.push_back(std::move(entry));
        }
        parameters["clips"] = std::move(clips);
    }

    void AnimatorComponent::LoadClipsFromJson(const json& parameters)
    {
        if (!parameters.is_object()) {
            return;
        }
        const auto it = parameters.find("clips");
        if (it == parameters.end() || !it->is_array()) {
            return;
        }

        std::vector<AnimationClipDesc> clips;
        for (const json& entry : *it) {
            if (!entry.is_object()) {
                continue;
            }
            AnimationClipDesc clip;
            clip.name = JsonManager::SafeGet<std::string>(entry, "name", {});
            clip.sourceName = JsonManager::SafeGet<std::string>(entry, "sourceName", {});
            clip.file = JsonManager::SafeGet<std::string>(entry, "file", {});
            if (!clip.name.empty()) {
                clips.push_back(std::move(clip));
            }
        }
        clips_ = std::move(clips);
        clipController_.reset();
    }

    void AnimatorComponent::Awake()
    {
        awoken_ = true;
        ApplyToRenderer();
    }

    void AnimatorComponent::ApplyToRenderer()
    {
        bound_ = false;
        controllerEntry_.reset();
        GameObject* owner = GetOwner();
        auto* modelMgr = FindService<ModelManager>(*this);
        if (!owner || !modelMgr) {
            return;
        }

        const std::shared_ptr<const AnimatorControllerData> controller = ResolveController();
        if (!controller) {
            return;
        }
        std::shared_ptr<const Animation> initialClip = AcquireInitialClip(*controller);

        // 描画はスキニング経路（頂点変形は Skinning.CS.hlsl が行う）。
        // 追加順を問わないよう、既にあれば流し込んで作り直させる。
        if (!modelPath_.empty()) {
            auto* renderer = owner->GetOrAddComponent<MeshRendererComponent>();
            renderer->SetSkinnedModelFile(modelPath_, std::move(initialClip));
            renderer->ReloadFromSpec();
            return;
        }
        if (auto* renderer = owner->GetComponent<MeshRendererComponent>(); renderer && renderer->UseSkinning(std::move(initialClip))) {
            renderer->ReloadFromSpec();
        }
    }

    void AnimatorComponent::ResolveSiblings() const
    {
        CachedSibling(renderer_);
        CachedSibling(transform_);
    }

    void AnimatorComponent::Start()
    {
        ResolveSiblings();
    }

    void AnimatorComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        const std::string_view name{ property.name };
        if (name != "controller" && name != "model") {
            return;
        }
        if (name == "model") {
            clipController_.reset();
        }
        controllerEntry_.reset();
        bound_ = false;
        if (awoken_) {
            ApplyToRenderer();
        }
    }

    std::string AnimatorComponent::ClipModelPath() const
    {
        if (!modelPath_.empty()) {
            return modelPath_;
        }
        ResolveSiblings();
        return renderer_ ? renderer_->GetModelAsset().path : std::string{};
    }

    std::shared_ptr<const AnimatorControllerData> AnimatorComponent::MakeClipController() const
    {
        auto data = std::make_shared<AnimatorControllerData>(AnimatorControllerFormat::MakeEmpty());
        AnimatorLayer& layer = data->layers.front();
        const std::string modelPath = ClipModelPath();
        for (const AnimationClipDesc& desc : clips_) {
            if (data->FindClip(desc.name)) {
                continue;
            }
            AnimatorClip clip;
            clip.id = desc.name;
            clip.file = desc.file.empty() ? modelPath : desc.file;
            clip.source = desc.sourceName;
            data->clips.push_back(std::move(clip));

            AnimatorState state;
            state.name = desc.name;
            state.clip = desc.name;
            layer.states.push_back(std::move(state));
        }
        if (!layer.states.empty()) {
            layer.defaultState = layer.states.front().name;
        }
        return data;
    }

    std::shared_ptr<const AnimatorControllerData> AnimatorComponent::ResolveController() const
    {
        if (controller_.IsSet()) {
            if (!controllerEntry_) {
                controllerEntry_ = AnimatorControllerLibrary::Get().Load(controller_.GetPath());
            }
            return controllerEntry_ ? controllerEntry_->data : nullptr;
        }
        if (clips_.empty()) {
            return nullptr;
        }
        if (!clipController_) {
            clipController_ = MakeClipController();
        }
        return clipController_;
    }

    std::shared_ptr<const Animation> AnimatorComponent::AcquireInitialClip(const AnimatorControllerData& controller) const
    {
        if (controller.layers.empty()) {
            return nullptr;
        }
        const AnimatorLayer& layer = controller.layers.front();
        const AnimatorState* state = layer.FindState(layer.defaultState);
        if (!state && !layer.states.empty()) {
            state = &layer.states.front();
        }
        const AnimatorClip* clip = state ? controller.FindClip(state->clip) : nullptr;
        auto* modelMgr = FindService<ModelManager>(*this);
        if (!clip || clip->file.empty() || !modelMgr) {
            return nullptr;
        }
        return modelMgr->AcquireClip(clip->file, clip->source);
    }

    void AnimatorComponent::EnsureBound() const
    {
        ResolveSiblings();
        Model* const model = renderer_ ? renderer_->GetModel() : nullptr;
        const ModelResource* const resource = model ? model->GetModelResource() : nullptr;
        const Skeleton* const skeleton =
            model && model->HasSkinCluster() && resource && resource->GetSkeleton() ? &*resource->GetSkeleton() : nullptr;
        const bool revisionChanged = controllerEntry_ && controllerEntry_->revision != boundRevision_;
        if (bound_ && model == boundModel_ && skeleton == boundSkeleton_ && !revisionChanged) {
            return;
        }

        bound_ = true;
        boundModel_ = model;
        boundSkeleton_ = skeleton;
        const std::shared_ptr<const AnimatorControllerData> controller = ResolveController();
        boundRevision_ = controllerEntry_ ? controllerEntry_->revision : 0;

        std::vector<std::shared_ptr<const Animation>> clips;
        if (controller) {
            auto* modelMgr = FindService<ModelManager>(*this);
            clips.reserve(controller->clips.size());
            for (const AnimatorClip& clip : controller->clips) {
                clips.push_back(modelMgr && !clip.file.empty() ? modelMgr->AcquireClip(clip.file, clip.source) : nullptr);
            }
        }
        const GameObject* const owner = GetOwner();
        runtime_.Build(controller, std::move(clips), skeleton, owner ? owner->GetName() : std::string{});
    }

    void AnimatorComponent::Update()
    {
        EnsureBound();
        if (!runtime_.IsReady() || !boundModel_) {
            return;
        }

        runtime_.Update(Time::DeltaTime() * speed_);
        boundModel_->UpdateSkinCluster(runtime_.GetSkeletonSpaceMatrices());

        if (skeletonDebugDrawEnabled_) {
            DrawSkeletonDebugLines();
        }
    }

    // ===== パラメータ =====

    void AnimatorComponent::SetFloat(const std::string& name, float value) { EnsureBound(); runtime_.SetFloat(name, value); }
    float AnimatorComponent::GetFloat(const std::string& name) { EnsureBound(); return runtime_.GetFloat(name); }
    void AnimatorComponent::SetInt(const std::string& name, int value) { EnsureBound(); runtime_.SetInt(name, value); }
    int AnimatorComponent::GetInt(const std::string& name) { EnsureBound(); return runtime_.GetInt(name); }
    void AnimatorComponent::SetBool(const std::string& name, bool value) { EnsureBound(); runtime_.SetBool(name, value); }
    bool AnimatorComponent::GetBool(const std::string& name) { EnsureBound(); return runtime_.GetBool(name); }
    void AnimatorComponent::SetTrigger(const std::string& name) { EnsureBound(); runtime_.SetTrigger(name); }
    void AnimatorComponent::ResetTrigger(const std::string& name) { EnsureBound(); runtime_.ResetTrigger(name); }

    // ===== 状態 =====

    void AnimatorComponent::Play(const std::string& stateName)
    {
        EnsureBound();
        runtime_.Play(stateName);
    }

    void AnimatorComponent::CrossFade(const std::string& stateName, float seconds)
    {
        EnsureBound();
        runtime_.CrossFade(stateName, seconds);
    }

    const std::string& AnimatorComponent::GetCurrentState() const
    {
        EnsureBound();
        return runtime_.GetCurrentStateName();
    }

    bool AnimatorComponent::IsInState(const std::string& stateName) const
    {
        EnsureBound();
        return runtime_.IsInState(stateName);
    }

    float AnimatorComponent::GetNormalizedTime() const
    {
        EnsureBound();
        return runtime_.GetNormalizedTime();
    }

    bool AnimatorComponent::IsInTransition() const
    {
        EnsureBound();
        return runtime_.IsInTransition();
    }

    std::optional<bool> AnimatorComponent::LoopOverride(bool loop) const
    {
        return controller_.IsSet() ? std::nullopt : std::optional<bool>(loop);
    }

    bool AnimatorComponent::Switch(const std::string& clipName, bool loop)
    {
        EnsureBound();
        return runtime_.Play(clipName, LoopOverride(loop));
    }

    bool AnimatorComponent::SwitchWithBlend(const std::string& clipName, float blendDuration, bool loop)
    {
        EnsureBound();
        return runtime_.CrossFade(clipName, blendDuration, LoopOverride(loop));
    }

    std::vector<std::string> AnimatorComponent::GetClipNames() const
    {
        std::vector<std::string> names;
        const std::shared_ptr<const AnimatorControllerData> controller = ResolveController();
        if (!controller) {
            return names;
        }
        names.reserve(controller->clips.size());
        for (const AnimatorClip& clip : controller->clips) {
            names.push_back(clip.id);
        }
        return names;
    }

    AnimatorRuntime& AnimatorComponent::GetRuntime() const
    {
        EnsureBound();
        return runtime_;
    }

    // ===== ジョイント参照 =====

    const Skeleton* AnimatorComponent::GetSkeleton() const
    {
        EnsureBound();
        return runtime_.IsReady() ? runtime_.GetSkeleton() : nullptr;
    }

    std::span<const Matrix4x4> AnimatorComponent::GetJointMatrices() const
    {
        EnsureBound();
        return runtime_.IsReady() ? std::span<const Matrix4x4>(runtime_.GetSkeletonSpaceMatrices()) : std::span<const Matrix4x4>{};
    }

    std::optional<Matrix4x4> AnimatorComponent::GetJointWorldMatrix(const std::string& jointName) const
    {
        const Skeleton* skeleton = GetSkeleton();
        if (!skeleton || !transform_) {
            return std::nullopt;
        }

        auto it = skeleton->jointMap.find(jointName);
        if (it == skeleton->jointMap.end()) {
            return std::nullopt;
        }

        const std::span<const Matrix4x4> matrices = GetJointMatrices();
        const size_t index = static_cast<size_t>(it->second);
        if (index >= matrices.size()) {
            return std::nullopt;
        }

        // 骨の行列はモデルローカル。オブジェクトのワールド行列を掛けて
        // ワールド空間へ変換する（行ベクトル規約なので子 → 親の順で掛ける）。
        return matrices[index] * transform_->Get().GetWorldMatrix();
    }

    std::optional<Vector3> AnimatorComponent::GetJointWorldPosition(const std::string& jointName) const
    {
        const std::optional<Matrix4x4> worldMatrix = GetJointWorldMatrix(jointName);
        if (!worldMatrix) {
            return std::nullopt;
        }
        return MathCore::CoordinateTransform::TransformCoord(Vector3{ 0.0f, 0.0f, 0.0f }, *worldMatrix);
    }

    void AnimatorComponent::DrawSkeletonDebugLines() const
    {
        const Skeleton* skeleton = GetSkeleton();
        if (!skeleton || !transform_) {
            return;
        }
        const std::span<const Matrix4x4> matrices = GetJointMatrices();
        if (matrices.size() != skeleton->joints.size()) {
            return;
        }

        const Matrix4x4 objectWorld = transform_->Get().GetWorldMatrix();
        auto& lineManager = LineManager::GetInstance();

        // ジョイントのワールド座標を一度だけ計算して使い回す
        std::vector<Vector3> jointPositions;
        jointPositions.reserve(skeleton->joints.size());
        for (const Matrix4x4& matrix : matrices) {
            const Matrix4x4 jointWorld = matrix * objectWorld;
            jointPositions.push_back(
                MathCore::CoordinateTransform::TransformCoord(Vector3{ 0.0f, 0.0f, 0.0f }, jointWorld));
        }

        for (const Joint& joint : skeleton->joints) {
            const Vector3& position = jointPositions[joint.index];

            // 親がいれば親との間に骨を描く。ルートは骨の相手がいないので線は引かない。
            // 骨はメッシュの内側にあるため、深度テストを切らないとモデルに隠れて見えない。
            if (joint.parent) {
                lineManager.DrawLine(jointPositions[*joint.parent], position, kBoneColor, 1.0f, kDrawThroughMesh);
            }

            // ジョイント自体の位置がわかるよう小さな十字を描く
            lineManager.DrawCross(position, kJointMarkerSize, kJointColor, 1.0f, kDrawThroughMesh);
        }
    }
}

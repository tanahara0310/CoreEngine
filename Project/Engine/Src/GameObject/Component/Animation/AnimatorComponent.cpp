#include "pch.h"
#include "AnimatorComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Model/Animation/AnimationPlayer.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/FrameRateController.h"

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

    void AnimatorComponent::Awake()
    {
        if (modelPath_.empty() || clips_.empty()) {
            return;  // 兄弟が既に持っているスキニングモデルへ後付けするケース
        }

        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* modelMgr = engine ? engine->GetService<ModelManager>() : nullptr;
        if (!modelMgr) { return; }

        // スケルトン生成より前に全クリップを読む（同じ ModelResource へ名前付きで登録される）。
        // 後から読んでも既に作られた AnimationPlayer の選択肢には入らない。
        for (const AnimationClipDesc& clip : clips_) {
            AnimationLoadInfo info;
            info.modelFile = modelPath_;
            info.animationName = clip.name;
            info.animationFile = clip.file.empty() ? modelPath_ : clip.file;
            info.sourceAnimationName = clip.sourceName;
            modelMgr->LoadAnimation(info);
        }

        // 描画はスキニング経路（頂点変形は Skinning.CS.hlsl が行う）。
        // 追加順を問わないよう、既にあれば流し込んで作り直させる。
        auto* renderer = owner->GetOrAddComponent<MeshRendererComponent>();
        renderer->SetSkinnedModelFile(modelPath_, clips_.front().name);
        renderer->ReloadFromSpec();
        currentClipName_ = clips_.front().name;
    }

    void AnimatorComponent::ResolveSiblings() const
    {
        if (!renderer_) { renderer_ = Sibling<MeshRendererComponent>(); }
        if (!transform_) { transform_ = Sibling<TransformComponent>(); }
    }

    void AnimatorComponent::Start()
    {
        ResolveSiblings();
    }

    AnimationPlayer* AnimatorComponent::GetPlayer() const
    {
        ResolveSiblings();
        if (!renderer_) { return nullptr; }
        Model* model = renderer_->GetModel();
        return model ? model->GetAnimationPlayer() : nullptr;
    }

    void AnimatorComponent::Update()
    {
        ResolveSiblings();
        if (!renderer_) { return; }

        Model* model = renderer_->GetModel();
        if (!model || !model->GetAnimationPlayer()) { return; }

        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* frameRate = engine ? engine->GetService<FrameRateController>() : nullptr;
        if (!frameRate) { return; }

        model->UpdateAnimation(frameRate->GetDeltaTime());

        if (skeletonDebugDrawEnabled_) {
            DrawSkeletonDebugLines();
        }
    }

    bool AnimatorComponent::Switch(const std::string& clipName, bool loop)
    {
        AnimationPlayer* player = GetPlayer();
        if (!player || !player->Switch(clipName, loop)) {
            return false;
        }
        currentClipName_ = clipName;
        return true;
    }

    bool AnimatorComponent::SwitchWithBlend(const std::string& clipName, float blendDuration, bool loop)
    {
        AnimationPlayer* player = GetPlayer();
        if (!player || !player->SwitchWithBlend(clipName, blendDuration, loop)) {
            return false;
        }
        currentClipName_ = clipName;
        return true;
    }

    std::vector<std::string> AnimatorComponent::GetClipNames() const
    {
        std::vector<std::string> names;

        ResolveSiblings();
        const Model* model = renderer_ ? renderer_->GetModel() : nullptr;
        const ModelResource* resource = model ? model->GetModelResource() : nullptr;
        if (!resource) {
            return names;
        }

        const auto& animations = resource->GetAnimations();
        names.reserve(animations.size());
        for (const auto& [name, animation] : animations) {
            names.push_back(name);
        }
        return names;
    }

    const Skeleton* AnimatorComponent::GetSkeleton() const
    {
        const AnimationPlayer* player = GetPlayer();
        return player ? player->GetSkeleton() : nullptr;
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

        const Joint& joint = skeleton->joints[it->second];

        // skeletonSpaceMatrix はモデルローカル。オブジェクトのワールド行列を掛けて
        // ワールド空間へ変換する（行ベクトル規約なので子 → 親の順で掛ける）。
        return joint.skeletonSpaceMatrix * transform_->Get().GetWorldMatrix();
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

        const Matrix4x4 objectWorld = transform_->Get().GetWorldMatrix();
        auto& lineManager = LineManager::GetInstance();

        // ジョイントのワールド座標を一度だけ計算して使い回す
        std::vector<Vector3> jointPositions;
        jointPositions.reserve(skeleton->joints.size());
        for (const Joint& joint : skeleton->joints) {
            const Matrix4x4 jointWorld = joint.skeletonSpaceMatrix * objectWorld;
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

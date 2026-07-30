#include "pch.h"
#include "AnimatedModelObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Model/Animation/AnimationPlayer.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/FrameRateController.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    namespace {
        /// 骨（親→子）を結ぶ線の色
        constexpr Vector3 kBoneColor = { 0.2f, 1.0f, 0.3f };
        /// ジョイント位置を示すマーカーの色
        constexpr Vector3 kJointColor = { 1.0f, 0.85f, 0.1f };
        /// ジョイントマーカーの大きさ [m]
        constexpr float kJointMarkerSize = 0.02f;
        /// 骨はメッシュ内部にあるので深度テストを切って手前に描く（LineManager の depthTest 引数）
        constexpr bool kDrawThroughMesh = false;
    }

    void AnimatedModelObject::Initialize() {
        auto* engine = GetEngineSystem();
        auto* dxCommon = engine->GetComponent<DirectXCommon>();
        auto* modelMgr = engine->GetComponent<ModelManager>();

        // トランスフォームの初期化（GPU 定数バッファを確保）
        if (dxCommon) {
            transform_.Initialize(dxCommon->GetDevice());
        }

        // アニメーションを先にロード（スケルトン生成前に必要）
        const std::string modelPath = GetModelPath();
        if (!modelPath.empty() && modelMgr) {
            // 初期クリップ
            AnimationLoadInfo info;
            info.modelFile = modelPath;
            info.animationName = GetAnimationName();
            info.animationFile = GetAnimationFile();
            info.sourceAnimationName = GetSourceAnimationName();
            modelMgr->LoadAnimation(info);

            // 追加クリップ（同じ ModelResource へ名前付きで登録される）
            for (const AnimationClipDesc& clip : GetAdditionalAnimationClips()) {
                AnimationLoadInfo extra;
                extra.modelFile = modelPath;
                extra.animationName = clip.name;
                extra.animationFile = clip.file.empty() ? GetAnimationFile() : clip.file;
                extra.sourceAnimationName = clip.sourceName;
                modelMgr->LoadAnimation(extra);
            }

            // スケルトンアニメーションモデルとして生成
            model_ = modelMgr->CreateSkeletonModel(modelPath, GetAnimationName(), true);
            currentClipName_ = GetAnimationName();
        }

        // テクスチャのロード（パスが空でなければ）
        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            texture_ = TextureManager::GetInstance().Load(texPath);
            textureName_ = texPath;
        }

        // 派生クラスの追加初期化（座標・コライダー設定など）
        OnInitialize();
        SetActive(true);
    }

    void AnimatedModelObject::OnUpdate() {
        auto* fc = GetEngineSystem()->GetComponent<FrameRateController>();
        if (fc && model_ && model_->GetAnimationPlayer()) {
            model_->UpdateAnimation(fc->GetDeltaTime());

            // ここから先はスケルトンが今フレームの姿勢に更新済み。
            // ジョイント追従の処理はこの順序でないと 1 フレーム遅れる。
            OnAnimationUpdated();

            if (skeletonDebugDrawEnabled_) {
                DrawSkeletonDebugLines();
            }
        }
    }

    bool AnimatedModelObject::SwitchAnimation(const std::string& clipName, bool loop) {
        AnimationPlayer* player = model_ ? model_->GetAnimationPlayer() : nullptr;
        if (!player || !player->Switch(clipName, loop)) {
            return false;
        }
        currentClipName_ = clipName;
        return true;
    }

    bool AnimatedModelObject::SwitchAnimationWithBlend(const std::string& clipName, float blendDuration, bool loop) {
        AnimationPlayer* player = model_ ? model_->GetAnimationPlayer() : nullptr;
        if (!player || !player->SwitchWithBlend(clipName, blendDuration, loop)) {
            return false;
        }
        currentClipName_ = clipName;
        return true;
    }

    std::vector<std::string> AnimatedModelObject::GetAnimationClipNames() const {
        std::vector<std::string> names;

        const ModelResource* resource = model_ ? model_->GetModelResource() : nullptr;
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

    const Skeleton* AnimatedModelObject::GetSkeleton() const {
        if (!model_) {
            return nullptr;
        }
        const AnimationPlayer* player = model_->GetAnimationPlayer();
        return player ? player->GetSkeleton() : nullptr;
    }

    std::optional<Matrix4x4> AnimatedModelObject::GetJointWorldMatrix(const std::string& jointName) const {
        const Skeleton* skeleton = GetSkeleton();
        if (!skeleton) {
            return std::nullopt;
        }

        auto it = skeleton->jointMap.find(jointName);
        if (it == skeleton->jointMap.end()) {
            return std::nullopt;
        }

        const Joint& joint = skeleton->joints[it->second];

        // skeletonSpaceMatrix はモデルローカル。オブジェクトのワールド行列を掛けて
        // ワールド空間へ変換する（行ベクトル規約なので子 → 親の順で掛ける）。
        return MathCore::Matrix::Multiply(joint.skeletonSpaceMatrix, transform_.GetWorldMatrix());
    }

    std::optional<Vector3> AnimatedModelObject::GetJointWorldPosition(const std::string& jointName) const {
        const std::optional<Matrix4x4> worldMatrix = GetJointWorldMatrix(jointName);
        if (!worldMatrix) {
            return std::nullopt;
        }
        return MathCore::CoordinateTransform::TransformCoord(Vector3{ 0.0f, 0.0f, 0.0f },*worldMatrix);
    }

    void AnimatedModelObject::DrawSkeletonDebugLines() const {
        const Skeleton* skeleton = GetSkeleton();
        if (!skeleton) {
            return;
        }

        const Matrix4x4 objectWorld = transform_.GetWorldMatrix();
        auto& lineManager = LineManager::GetInstance();

        // ジョイントのワールド座標を一度だけ計算して使い回す
        std::vector<Vector3> jointPositions;
        jointPositions.reserve(skeleton->joints.size());
        for (const Joint& joint : skeleton->joints) {
            const Matrix4x4 jointWorld = MathCore::Matrix::Multiply(joint.skeletonSpaceMatrix, objectWorld);
            jointPositions.push_back(
                MathCore::CoordinateTransform::TransformCoord(Vector3{ 0.0f, 0.0f, 0.0f },jointWorld));
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

#ifdef USE_IMGUI
    int AnimatedModelObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
        const int baseCount = ModelGameObject::GetInspectorTabs(outTabs, maxTabs);
        if (baseCount == 0 || maxTabs < baseCount + 1) {
            return baseCount;
        }

        outTabs[baseCount] = { "deltaTime.png", "アニメーション",
            {0.30f, 0.85f, 0.55f, 1.0f}, {0.30f, 0.85f, 0.55f, 0.25f} };
        return baseCount + 1;
    }

    bool AnimatedModelObject::DrawInspectorTabContent(int tabIndex) {
        // 基底のタブ数は基底クラスに問い合わせる（インデックスの直書きを避ける）
        InspectorTabDef scratch[16]{};
        const int baseCount = ModelGameObject::GetInspectorTabs(scratch, 16);

        if (tabIndex < baseCount) {
            return ModelGameObject::DrawInspectorTabContent(tabIndex);
        }
        return DrawAnimationSection();
    }

    bool AnimatedModelObject::DrawAnimationSection() {
        bool changed = false;

        const AnimationPlayer* player = model_ ? model_->GetAnimationPlayer() : nullptr;
        if (!player) {
            ImGui::TextDisabled("アニメーションを持たないモデルです");
            return false;
        }

        ImGui::Text("再生時刻: %.3f s", player->GetTime());

        if (const Skeleton* skeleton = GetSkeleton()) {
            ImGui::Text("ジョイント数: %d", static_cast<int>(skeleton->joints.size()));
        }

        // ===== クリップ切り替え（アニメーションブレンド） =====
        const std::vector<std::string> clipNames = GetAnimationClipNames();
        if (clipNames.size() > 1) {
            ImGui::Separator();
            ImGui::Text("現在のクリップ: %s", currentClipName_.c_str());
            if (player->IsBlending()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "(ブレンド中)");
            }

            ImGui::SliderFloat("ブレンド時間", &imguiBlendDuration_, 0.0f, 2.0f, "%.2f s");

            for (const std::string& clipName : clipNames) {
                if (clipName == currentClipName_) {
                    continue; // 同じクリップへのブレンドは意味がない
                }
                if (ImGui::Button(clipName.c_str())) {
                    SwitchAnimationWithBlend(clipName, imguiBlendDuration_);
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }

        ImGui::Separator();

        if (ImGui::Checkbox("骨のデバッグ表示", &skeletonDebugDrawEnabled_)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("スケルトンの親子関係を線で、ジョイント位置を十字で描画します");
        }

        return changed;
    }
#endif // USE_IMGUI

}  // namespace CoreEngine

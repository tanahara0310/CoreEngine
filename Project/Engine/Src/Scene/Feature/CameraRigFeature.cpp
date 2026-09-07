#include "pch.h"
#include "CameraRigFeature.h"

#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraSceneStateIO.h"
#include "Scene/SceneSaveSystem.h"
#include "Camera/Rig/CameraRig.h"
#include "Camera/Sequence/CameraSequenceEvaluator.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Utility/FrameRate/Time.h"

#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// @brief 名前でオブジェクトを引く
        GameObject* FindObject(GameObjectManager* objects, const std::string& name)
        {
            if (!objects || name.empty()) {
                return nullptr;
            }

            for (const auto& object : objects->GetAllObjects()) {
                if (object && object->GetName() == name) {
                    return object.get();
                }
            }
            return nullptr;
        }

        /// @brief オブジェクトの回転を取り出す（Transform を持たなければ無回転）
        /// @details 回転を要るのは Target 指定のオフセットと OrbitTarget だけ。
        ///          持っていない対象でも World 指定なら問題なく動く。
        Vector3 GetObjectRotation(GameObject* object)
        {
            if (!object) {
                return { 0.0f, 0.0f, 0.0f };
            }

            if (auto* transform = object->GetComponent<TransformComponent>()) {
                return transform->Rotate();
            }
            return { 0.0f, 0.0f, 0.0f };
        }

        /// @brief リグが参照している対象名を集める
        void CollectTargetNames(const CameraRigAsset& asset, std::vector<std::string>& outNames)
        {
            const auto push = [&outNames](const CameraRigTargetRef& ref) {
                if (!ref.objectName.empty()) {
                    outNames.push_back(ref.objectName);
                }
            };

            push(asset.body.target);
            for (const auto& ref : asset.body.targets) {
                push(ref);
            }
            push(asset.aim.target);
            for (const auto& ref : asset.aim.targets) {
                push(ref);
            }
        }
    }

    void CameraRigFeature::Initialize(SceneContext&)
    {
        CameraRig::SetActiveRuntime(&runtime_);
    }

    void CameraRigFeature::PostSceneInitialize(SceneContext& ctx)
    {
        if (!ctx.saveSystem) {
            return;
        }

        const std::string rigName =
            CameraSceneStateIO::LoadStartupRigName(ctx.saveSystem->GetSceneName());
        if (rigName.empty()) {
            return;
        }

        // シーンの開始時点では繋ぎ元になる構図が無いので、繋がずにそのまま置く。
        // 読み込めなければ何もしない（ライブラリ側がログを出す）。
        CameraRig::Activate(rigName);
    }

    void CameraRigFeature::UpdateTargetVelocities(GameObjectManager* objects, float deltaTime)
    {
        const CameraRigAsset* asset = runtime_.GetAsset();
        if (!asset || deltaTime <= 0.0f) {
            return;
        }

        std::vector<std::string> names;
        CollectTargetNames(*asset, names);

        // 参照が変わったときに古い名前を残さない。速度は 1 フレームぶんの情報なので
        // 作り直しで問題ない。
        velocities_.clear();

        for (const auto& name : names) {
            GameObject* object = FindObject(objects, name);
            if (!object) {
                lastPositions_.erase(name);
                continue;
            }

            const Vector3 position = object->GetWorldPosition();
            const auto found = lastPositions_.find(name);
            if (found != lastPositions_.end()) {
                velocities_[name] = (position - found->second) * (1.0f / deltaTime);
                found->second = position;
            } else {
                // 初出のフレームは前の位置が無いので速度 0。1 フレームだけ狭い画になるが、
                // ここで大きな値を入れると切り替えた瞬間に視野角が飛ぶ。
                lastPositions_.emplace(name, position);
            }
        }
    }

    CameraRigContext CameraRigFeature::MakeContext(GameObjectManager* objects,
        float aspectRatio) const
    {
        CameraRigContext context{};
        context.aspectRatio = aspectRatio;
        context.resolveTarget = [this, objects](const std::string& name,
            CameraRigTargetState& outState) {
            GameObject* object = FindObject(objects, name);
            if (!object) {
                return false;
            }

            outState.position = object->GetWorldPosition();
            outState.rotation = GetObjectRotation(object);

            const auto found = velocities_.find(name);
            if (found != velocities_.end()) {
                outState.velocity = found->second;
                outState.hasVelocity = true;
            }
            return true;
        };
        return context;
    }

    void CameraRigFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostLogic) {
            return;
        }

        const float deltaTime = Time::DeltaTime();
        runtime_.Update(deltaTime, Time::UnscaledDeltaTime());

        if (!runtime_.IsActive()) {
            hasBlendFrom_ = false;
            observedActivationId_ = 0;
            lastPositions_.clear();
            velocities_.clear();
            return;
        }

        // 書き込むのは常にゲーム視点カメラ。ctx.gameViewCamera3D は「今覗いているカメラ」
        // なので、エディタ視点に切り替えている間はエディタカメラを指す。そちらは奪わない。
        Camera* camera = ctx.cameraManager
            ? ctx.cameraManager->GetCamera(ctx.cameraManager->GetGameCameraName())
            : nullptr;

        if (!camera) {
            return;
        }

        // 切り替わったフレームで、その時点の構図を繋ぎ元として控える。前のリグの出力でも、
        // ゲーム側の追従が決めた構図でも、同じように繋ぎ元になる。
        if (runtime_.GetActivationId() != observedActivationId_) {
            observedActivationId_ = runtime_.GetActivationId();
            blendFrom_ = camera->CaptureSnapshot("RigBlendFrom");
            hasBlendFrom_ = true;
        }

        // 速度はスケールされた時間で求める。位置の変化も時間スケールを受けているので、
        // 同じ物差しで割らないと、ヒットストップ中に速さが 0 に見えて視野角が縮む。
        UpdateTargetVelocities(ctx.gameObjectManager, deltaTime);

        // アスペクト比は画面内の構図を出すのに要る。0（自動）ならリグ側が 16:9 とみなす。
        const CameraRigContext context =
            MakeContext(ctx.gameObjectManager, camera->GetParameters().aspectRatio);

        // カメラの現在値から始めて、リグが決める 3 つ（位置・回転・視野角）だけ差し替える。
        // 近クリップ・遠クリップ・アスペクト比はカメラ側の設定を保つ。
        CameraSnapshot pose = camera->CaptureSnapshot("Rig");
        if (!runtime_.Evaluate(deltaTime, &context, pose)) {
            // 対象が消えていれば何も書かない。ゲーム側の追従が決めた構図がそのまま残る。
            return;
        }

        const float blendWeight = runtime_.GetBlendWeight();
        if (hasBlendFrom_ && blendWeight < 1.0f) {
            // 姿勢どうしの補間はシーケンス側と同じものを使う。角度の巻き戻しを
            // 含めて 1 か所にまとめておきたい。
            pose = CameraSequenceEvaluator::Interpolate(
                blendFrom_, pose, blendWeight, EasingUtil::Type::EaseInOutCubic);
        }

        camera->RestoreSnapshot(pose);

        // CameraManager の通常更新（FrameStart）より後に姿勢を変えるため、
        // 描画前にここで行列と GPU 定数バッファを作り直す。
        camera->UpdateMatrix();
    }

    void CameraRigFeature::PostSceneFinalize(SceneContext&)
    {
        runtime_.Deactivate();
        hasBlendFrom_ = false;
        observedActivationId_ = 0;
        lastPositions_.clear();
        velocities_.clear();

        if (CameraRig::GetActiveRuntime() == &runtime_) {
            CameraRig::SetActiveRuntime(nullptr);
        }
    }
}

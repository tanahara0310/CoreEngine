#include "pch.h"
#include "CameraSequenceFeature.h"

#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Camera/Sequence/CameraSequence.h"
#include "Camera/Sequence/CameraSequenceEvaluator.h"
#include "Camera/Sequence/CameraSequenceEvent.h"
#include "Camera/Shake/CameraShake.h"
#include "Utility/Event/EventBus.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Utility/FrameRate/Time.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 名前でシーンのオブジェクトを引く注視解決口を作る
        /// @details 評価器はシーンを知らないので、ここで橋渡しする。
        ///          対象が消えていれば false を返し、そのキーは保存された回転へ落ちる。
        CameraSequenceAimContext MakeAimContext(GameObjectManager* objects)
        {
            CameraSequenceAimContext context{};
            if (!objects) {
                return context;
            }

            context.resolveObject = [objects](const std::string& name, Vector3& outPosition) {
                for (const auto& object : objects->GetAllObjects()) {
                    if (object && object->GetName() == name) {
                        outPosition = object->GetWorldPosition();
                        return true;
                    }
                }
                return false;
            };
            return context;
        }
    }

    void CameraSequenceFeature::DispatchEvent(const CameraSequenceEvent& event)
    {
        switch (event.type) {
        case CameraSequenceEventType::Shake:
            // プリセットが見つからなくても黙って何もしない。演出が出ないだけで
            // 再生は続けたいし、毎フレーム同じログを吐くと他が読めなくなる。
            CameraShake::PlayPreset(event.name, event.value);
            break;

        case CameraSequenceEventType::Trauma:
            CameraShake::AddTrauma(event.value);
            break;

        case CameraSequenceEventType::TimeScale:
            // 戻す時刻を控えて、Update 側で戻す。
            Time::SetTimeScale(event.value);
            timeScaleRemaining_ = event.duration;
            break;

        case CameraSequenceEventType::Callback:
            EventBus::GetInstance().Publish(CameraSequenceCallbackEvent{ event.name, event.value });
            break;
        }
    }

    void CameraSequenceFeature::UpdateTimeScaleOverride(float unscaledDeltaTime)
    {
        if (timeScaleRemaining_ <= 0.0f) {
            return;
        }

        // スロー中は Time::DeltaTime() 自体が縮むので、戻すまでの計測は
        // 必ずスケールされない時間で行う。
        timeScaleRemaining_ -= unscaledDeltaTime;
        if (timeScaleRemaining_ <= 0.0f) {
            timeScaleRemaining_ = 0.0f;
            Time::SetTimeScale(1.0f);
        }
    }

    void CameraSequenceFeature::Initialize(SceneContext&)
    {
        CameraSequence::SetActivePlayer(&player_);
    }

    void CameraSequenceFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostLogic) {
            return;
        }

        UpdateTimeScaleOverride(Time::UnscaledDeltaTime());

        // カメラが無くても時間は進める（シーン切り替え中に再生が凍りつかないように）
        player_.Update(Time::DeltaTime(), Time::UnscaledDeltaTime());

        // 跨いだイベントは、カメラの有無に関わらず実行する。カメラが一瞬いない
        // フレームで演出だけ落ちると、原因の分からない取りこぼしになる。
        for (const auto& event : player_.GetFiredEvents()) {
            DispatchEvent(event);
        }

        if (!player_.IsActive()) {
            hasBlendFrom_ = false;
            observedPlayId_ = 0;
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

        // 新しい再生が始まったフレームで、ゲーム側が決めた構図を繋ぎ元として控える。
        // この時点のカメラはすでに追従コンポーネント（OnLateUpdate）が書き終えている。
        if (player_.GetPlayId() != observedPlayId_) {
            observedPlayId_ = player_.GetPlayId();
            blendFrom_ = camera->CaptureSnapshot("SequenceBlendFrom");
            hasBlendFrom_ = true;
        }

        // 注視対象は毎フレーム引き直す。対象が動けばカメラの目もついていく。
        const CameraSequenceAimContext aimContext = MakeAimContext(ctx.gameObjectManager);

        CameraSnapshot pose{};
        if (!player_.Evaluate(pose, &aimContext)) {
            return;
        }

        const float blendWeight = player_.GetBlendInWeight();
        if (hasBlendFrom_ && blendWeight < 1.0f) {
            pose = CameraSequenceEvaluator::Interpolate(
                blendFrom_, pose, blendWeight, EasingUtil::Type::EaseInOutCubic);
        }

        camera->RestoreSnapshot(pose);

        // CameraManager の通常更新（FrameStart）より後に姿勢を変えるため、
        // 描画前にここで行列と GPU 定数バッファを作り直す。
        camera->UpdateMatrix();
    }

    void CameraSequenceFeature::PostSceneFinalize(SceneContext&)
    {
        player_.Stop();
        hasBlendFrom_ = false;
        observedPlayId_ = 0;

        // スロー演出の途中でシーンが終わっても、時間スケールは必ず戻す。
        if (timeScaleRemaining_ > 0.0f) {
            timeScaleRemaining_ = 0.0f;
            Time::SetTimeScale(1.0f);
        }

        if (CameraSequence::GetActivePlayer() == &player_) {
            CameraSequence::SetActivePlayer(nullptr);
        }
    }
}

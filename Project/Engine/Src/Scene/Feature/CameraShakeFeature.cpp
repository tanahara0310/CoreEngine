#include "pch.h"
#include "CameraShakeFeature.h"

#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakeEvent.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/Time.h"

#include <cmath>

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    namespace
    {
        /// @brief 回転行列（行ベクトル規約の Rx * Ry * Rz）からオイラー角を取り出す
        /// @details Matrix::MakeAffine と対になる分解。MathCore の DecomposeToSRT は
        ///          別の合成順（Rz * Rx * Ry）を前提にした式なので、ここでは使えない。
        /// @note 行 0/1/2 がそれぞれローカル X/Y/Z 軸。Camera::LookAt と同じく行 2 が視線方向。
        Vector3 ExtractEulerXYZ(const Matrix4x4& matrix)
        {
            // M[0][2] = -sin(yaw)
            const float sinYaw = Clamp(-matrix.m[0][2], -1.0f, 1.0f);
            const float yaw = std::asin(sinYaw);
            const float cosYaw = std::cos(yaw);

            // cos(yaw) が 0 に潰れるとピッチとロールを分離できない（ジンバルロック）。
            // ロールを 0 に固定してピッチだけ取り出す
            if (std::abs(cosYaw) < 1.0e-5f) {
                return { std::atan2(matrix.m[1][0] * sinYaw, matrix.m[1][1]), yaw, 0.0f };
            }

            return {
                std::atan2(matrix.m[1][2], matrix.m[2][2]), // pitch
                yaw,
                std::atan2(matrix.m[0][1], matrix.m[0][0]), // roll
            };
        }

        /// @brief 揺れの平行移動をワールド空間の 1 本のベクトルにまとめる
        Vector3 ToWorldTranslation(const Vector3& baseRotate, const CameraShakeOffset& offset)
        {
            Vector3 world = offset.worldPosition;

            if (LengthSquared(offset.localPosition) > 0.0f) {
                const Matrix4x4 basis =
                    Matrix::MakeAffine({ 1.0f, 1.0f, 1.0f }, baseRotate, { 0.0f, 0.0f, 0.0f });

                world += basis.GetAxisX() * offset.localPosition.x
                       + basis.GetAxisY() * offset.localPosition.y
                       + basis.GetAxisZ() * offset.localPosition.z;
            }

            return world;
        }

        /// @brief 基準姿勢へカメラローカルの揺れを合成したオイラー角を返す
        /// @details オイラー角を単純に足すと rotate.z が「ワールド Z 軸まわり」の回転になり、
        ///          カメラが下を向いているほどロールがヨーに化ける。行列で合成してから
        ///          分解し直すことで、ロールを必ず視線軸まわりに保つ。
        Vector3 ComposeRotation(const Vector3& baseRotate, const Vector3& localRotation)
        {
            if (LengthSquared(localRotation) <= 0.0f) {
                return baseRotate;
            }

            // 行ベクトル規約なので、先に効かせたい変換が左。
            // ローカル空間で揺らしてから基準姿勢でワールドへ送る
            const Matrix4x4 composed =
                Matrix::MakeAffine({ 1.0f, 1.0f, 1.0f }, localRotation, { 0.0f, 0.0f, 0.0f })
                * Matrix::MakeAffine({ 1.0f, 1.0f, 1.0f }, baseRotate, { 0.0f, 0.0f, 0.0f });

            return ExtractEulerXYZ(composed);
        }

        /// @brief 揺れをカメラへ乗せて行列を確定し、基準姿勢を元に戻す
        /// @details Camera 側に加算オフセットの口を持たせない代わりに、この関数の中だけで
        ///          「退避 → 適用 → 行列確定 → 復元」を閉じる。間に他のコードが走らないので、
        ///          Camera::GetTranslate() / GetRotate() を読む側（追従・コントローラ・
        ///          エディタ UI）が揺れた値を観測することはない。
        /// @note 行列と GPU 定数バッファには揺れた姿勢が残り、復元されるのは
        ///       translate / rotate / parameters だけ。描画はこの残った行列を使う。
        ///       このため ViewInfo::position（Camera::GetPosition() 由来）だけは揺れ前の
        ///       位置になるが、そちらを見ているのはカリングと大気なので、
        ///       揺れで毎フレーム動かないほうがむしろ都合がよい。
        void ApplyShake(Camera& camera, const CameraShakeOffset& offset)
        {
            const Vector3 baseTranslate = camera.GetTranslate();
            const Vector3 baseRotate = camera.GetRotate();
            const CameraParameters baseParameters = camera.GetParameters();

            camera.SetTranslate(baseTranslate + ToWorldTranslation(baseRotate, offset));
            camera.SetRotate(ComposeRotation(baseRotate, offset.localRotation));

            if (offset.fovDegrees != 0.0f
                && baseParameters.projectionType == CameraProjectionType::Perspective) {
                CameraParameters shaken = baseParameters;
                shaken.SetFovDegrees(baseParameters.GetFovDegrees() + offset.fovDegrees);
                camera.SetParameters(shaken);
            }

            camera.UpdateMatrix();

            camera.SetTranslate(baseTranslate);
            camera.SetRotate(baseRotate);
            camera.SetParameters(baseParameters);
        }
    }

    void CameraShakeFeature::Initialize(SceneContext&)
    {
        // 入口その 1：静的ファサード（CameraShake::Play など）の委譲先になる
        CameraShake::SetActiveShaker(&shaker_);

        // 入口その 2：EventBus。発行側は誰が揺らすかを知らずに済む
        EventBus& bus = EventBus::GetInstance();

        subscriptions_.Add(bus.Subscribe<CameraShakeEvent>(
            [this](const CameraShakeEvent& event) {
                if (event.hasWorldOrigin) {
                    shaker_.Play(event.params, event.worldOrigin);
                } else {
                    shaker_.Play(event.params);
                }
            }));

        subscriptions_.Add(bus.Subscribe<CameraTraumaEvent>(
            [this](const CameraTraumaEvent& event) { shaker_.AddTrauma(event.amount); }));

        subscriptions_.Add(bus.Subscribe<CameraShakeStopEvent>(
            [this](const CameraShakeStopEvent& event) { shaker_.StopAll(event.fadeOutSeconds); }));
    }

    void CameraShakeFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostLogic) {
            return;
        }

        // 揺らすのは常にゲーム視点カメラ。ctx.gameViewCamera3D は「今覗いているカメラ」なので、
        // エディタ視点に切り替えている間はエディタカメラを指す。そちらを揺らしてはいけない。
        Camera* camera = ctx.cameraManager
            ? ctx.cameraManager->GetCamera(ctx.cameraManager->GetGameCameraName())
            : nullptr;

        CameraShakeContext shakeContext;
        shakeContext.listenerPosition = camera ? camera->GetTranslate() : Vector3{};
        shakeContext.scaledDeltaTime = Time::DeltaTime();
        shakeContext.unscaledDeltaTime = Time::UnscaledDeltaTime();

        // カメラが無くても時間は進める（シーン切り替え中に揺れが凍りつかないように）
        shaker_.Update(shakeContext);

        if (!camera) {
            return;
        }

        const CameraShakeOffset& offset = shaker_.GetOffset();
        if (offset.IsNegligible()) {
            return;
        }

        ApplyShake(*camera, offset);
    }

    void CameraShakeFeature::PostSceneFinalize(SceneContext&)
    {
        subscriptions_.Clear();
        shaker_.Reset();

        if (CameraShake::GetActiveShaker() == &shaker_) {
            CameraShake::SetActiveShaker(nullptr);
        }
    }
}

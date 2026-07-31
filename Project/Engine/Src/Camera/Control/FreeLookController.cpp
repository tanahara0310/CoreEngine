#include "pch.h"
#include "FreeLookController.h"

#include "Camera/Camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace CoreEngine::MathCore;

namespace CoreEngine
{
    namespace {
        // 真上・真下でのジンバル暴れを避けるためピッチを制限する
        constexpr float kMaxPitch = std::numbers::pi_v<float> *0.49f;
    }

    void FreeLookController::SetEnabled(bool enabled)
    {
        enabled_ = enabled;
        if (!enabled_) {
            looking_ = false;
        }
    }

    void FreeLookController::Update(const CameraInputState& input, float deltaTime, Camera& camera)
    {
        if (!enabled_ || !input.active) {
            looking_ = false;
            prevLookButton_ = input.lookButton;
            return;
        }

        // ===== 右ドラッグによる視点回転 =====
        // 開始は押し込みエッジのみ。押下中かどうかだけで判定すると、ビューポート外で
        // 押したままカーソルを持ち込んだときに意図しない回転が入る。
        if (input.lookButton && !prevLookButton_) {
            looking_ = true;
        }
        if (!input.lookButton) {
            looking_ = false;
        }
        prevLookButton_ = input.lookButton;

        if (looking_) {
            Vector3 rot = camera.GetRotate();
            rot.y += input.mouseDelta.x * settings_.lookSensitivity;                                  // ヨー
            rot.x += input.mouseDelta.y * settings_.lookSensitivity * (settings_.invertY ? -1.0f : 1.0f); // ピッチ
            rot.x = std::clamp(rot.x, -kMaxPitch, kMaxPitch);
            camera.SetRotate(rot);
        }

        // ===== WASD / Q・E による移動 =====
        // Camera::GetForward() は「カメラ行列の Z 軸の逆」を返す規約のため、
        // W（前進）では -forward 方向へ進める（従来の GameView 操作と同じ符号）。
        const Vector3 forward = camera.GetForward();
        const Vector3 right = camera.GetRight();

        Vector3 move = { 0.0f, 0.0f, 0.0f };
        if (input.forward) move = Vector::Subtract(move, forward);
        if (input.back)    move = Vector::Add(move, forward);
        if (input.right)   move = Vector::Add(move, right);
        if (input.left)    move = Vector::Subtract(move, right);
        if (input.up)      move.y += 1.0f;   // ワールドY軸上昇
        if (input.down)    move.y -= 1.0f;   // ワールドY軸下降

        const float len = std::sqrtf(move.x * move.x + move.y * move.y + move.z * move.z);
        if (len <= 0.0001f) {
            return;
        }

        const float speed = settings_.moveSpeed * (input.boost ? settings_.boostMultiplier : 1.0f) * deltaTime;
        Vector3 pos = camera.GetTranslate();
        pos.x += (move.x / len) * speed;
        pos.y += (move.y / len) * speed;
        pos.z += (move.z / len) * speed;
        camera.SetTranslate(pos);
    }

    void FreeLookController::ApplyTo(Camera& camera) const
    {
        // 自由移動はカメラの Transform 自体が状態なので、外から反映するものはない
        (void)camera;
    }

    void FreeLookController::Reset()
    {
        looking_ = false;
    }
}

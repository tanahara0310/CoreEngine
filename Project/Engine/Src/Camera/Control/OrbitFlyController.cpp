#include "pch.h"
#include "OrbitFlyController.h"

#include "Camera/Camera.h"

#include <algorithm>
#include <cmath>

using namespace CoreEngine::MathCore;

namespace CoreEngine
{
    namespace {
        // 真上・真下を少し手前で止める（極でヨーが暴れるのを避ける）
        constexpr float kMaxPitch = std::numbers::pi_v<float> * 0.49f; // 88.2度

        constexpr int kWheelThreshold = 120;   // DirectInput のホイール1クリック分
        constexpr int kMaxAccumulated = 1200;  // 10ノッチ分
        constexpr int kMaxStepsPerFrame = 3;
    }

    float OrbitFlyController::NormalizeAngle(float angle)
    {
        while (angle > std::numbers::pi_v<float>) {
            angle -= 2.0f * std::numbers::pi_v<float>;
        }
        while (angle < -std::numbers::pi_v<float>) {
            angle += 2.0f * std::numbers::pi_v<float>;
        }
        return angle;
    }

    Vector3 OrbitFlyController::ComputeEyePosition() const
    {
        const OrbitState& s = settings_.smoothMovement ? smooth_ : state_;
        return {
            s.target.x + s.distance * std::cosf(s.pitch) * std::sinf(s.yaw),
            s.target.y + s.distance * std::sinf(s.pitch),
            s.target.z + s.distance * std::cosf(s.pitch) * std::cosf(s.yaw)
        };
    }

    void OrbitFlyController::ApplyTo(Camera& camera) const
    {
        const OrbitState& s = settings_.smoothMovement ? smooth_ : state_;

        camera.SetTranslate(ComputeEyePosition());

        // 注視点を向く姿勢をオイラー角で表す。
        // 視点→注視点の方向は outward の逆なので、MakeAffine（Rx*Ry*Rz）の前方軸
        // (cosX sinY, -sinX, cosX cosY) と一致させると pitch はそのまま、yaw は +π になる。
        // これは Matrix::LookAt(eye, target, +Y) と厳密に同じビュー行列を与える
        // （pitch は ±0.49π にクランプされるため cos(pitch) > 0 が保証される）。
        camera.SetScale({ 1.0f, 1.0f, 1.0f });
        camera.SetRotate({ s.pitch, NormalizeAngle(s.yaw + std::numbers::pi_v<float>), 0.0f });
    }

    void OrbitFlyController::Update(const CameraInputState& input, float deltaTime, Camera& camera)
    {
        if (!input.active) {
            // 操作を受け付けない状態ではドラッグを打ち切り、貯めたホイール量も捨てる。
            // ボタン状態も忘れることで、ビューポートへ戻った瞬間に押しっぱなしの
            // ボタンでドラッグが再開しないようにする（開始は必ず押し込みエッジ）。
            orbiting_ = false;
            panning_ = false;
            accumulatedWheelDelta_ = 0;
            prevOrbitButton_ = input.orbitButton;
        } else {
            HandleOrbitAndPan(input);
            HandleZoom(input);
            HandleFlyMove(input, deltaTime);
        }

        if (settings_.smoothMovement) {
            UpdateSmoothMovement();
        } else {
            smooth_ = state_;
        }

        ApplyTo(camera);
    }

    void OrbitFlyController::HandleOrbitAndPan(const CameraInputState& input)
    {
        // ドラッグは「押し込んだ瞬間」に開始し、離すまで継続する。
        // 押下中かどうかだけで判定すると、ビューポート外で押したままカーソルを
        // 持ち込んだ場合や、入力デバイスの状態がフレーム途中で有効になった場合に
        // 意図しない回転が入る（実際にデバッグカメラの保存姿勢が回ってしまった）。
        const bool orbitEdge = input.orbitButton && !prevOrbitButton_;
        prevOrbitButton_ = input.orbitButton;

        if (orbitEdge) {
            // Shift + 中ドラッグ = パン、中ドラッグのみ = 回転（Blender と同じ割り当て）
            panning_ = input.panButton;
            orbiting_ = !input.panButton;
        }
        if (!input.orbitButton) {
            orbiting_ = false;
            panning_ = false;
        }

        if (orbiting_) {
            float pitchDelta = input.mouseDelta.y * settings_.rotationSensitivity;
            if (settings_.invertY) {
                pitchDelta = -pitchDelta;
            }
            state_.yaw = NormalizeAngle(state_.yaw + input.mouseDelta.x * settings_.rotationSensitivity);
            state_.pitch = std::clamp(state_.pitch + pitchDelta, -kMaxPitch, kMaxPitch);
        }

        if (panning_) {
            const Vector3 outward = {
                std::cosf(state_.pitch) * std::sinf(state_.yaw),
                std::sinf(state_.pitch),
                std::cosf(state_.pitch) * std::cosf(state_.yaw)
            };
            const Vector3 right = Vector::Normalize(Vector::Cross({ 0.0f, 1.0f, 0.0f }, outward));
            const Vector3 up = Vector::Normalize(Vector::Cross(outward, right));

            const float speed = settings_.panSensitivity;
            state_.target = Vector::Add(state_.target, Vector::Multiply(input.mouseDelta.x * speed, right));
            state_.target = Vector::Add(state_.target, Vector::Multiply(input.mouseDelta.y * speed, up));
            ClampTargetToWorldBounds();
        }
    }

    void OrbitFlyController::HandleZoom(const CameraInputState& input)
    {
        if (input.wheelDelta == 0) {
            return;
        }

        accumulatedWheelDelta_ = std::clamp(
            accumulatedWheelDelta_ + input.wheelDelta, -kMaxAccumulated, kMaxAccumulated);

        if (std::abs(accumulatedWheelDelta_) < kWheelThreshold) {
            return;
        }

        int wheelSteps = accumulatedWheelDelta_ / kWheelThreshold;
        wheelSteps = std::clamp(wheelSteps, -kMaxStepsPerFrame, kMaxStepsPerFrame);

        // distance を変えず target ごと前後させる。distance 比例のステップだと
        // 寄ると minDistance で頭打ち、遠ざかるとステップが際限なく大きくなる。
        const float dolly = static_cast<float>(wheelSteps) * settings_.zoomSensitivity;
        const Vector3 forward = {
            -std::cosf(state_.pitch) * std::sinf(state_.yaw),
            -std::sinf(state_.pitch),
            -std::cosf(state_.pitch) * std::cosf(state_.yaw)
        };
        state_.target = Vector::Add(state_.target, Vector::Multiply(dolly, forward));
        ClampTargetToWorldBounds();

        // 持ち越しで連続ジャンプしないよう処理後はリセット
        accumulatedWheelDelta_ = 0;
    }

    void OrbitFlyController::HandleFlyMove(const CameraInputState& input, float deltaTime)
    {
        const Vector3 outward = {
            std::cosf(state_.pitch) * std::sinf(state_.yaw),
            std::sinf(state_.pitch),
            std::cosf(state_.pitch) * std::cosf(state_.yaw)
        };
        const Vector3 forward = Vector::Multiply(-1.0f, outward);
        const Vector3 right = Vector::Normalize(Vector::Cross({ 0.0f, 1.0f, 0.0f }, outward));
        const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };

        Vector3 move = { 0.0f, 0.0f, 0.0f };
        if (input.forward) move = Vector::Add(move, forward);
        if (input.back)    move = Vector::Add(move, Vector::Multiply(-1.0f, forward));
        if (input.right)   move = Vector::Add(move, right);
        if (input.left)    move = Vector::Add(move, Vector::Multiply(-1.0f, right));
        if (input.up)      move = Vector::Add(move, worldUp);
        if (input.down)    move = Vector::Add(move, Vector::Multiply(-1.0f, worldUp));

        const float moveLenSq = move.x * move.x + move.y * move.y + move.z * move.z;
        if (moveLenSq <= 1e-10f) {
            return;
        }
        move = Vector::Normalize(move);

        const float speed = settings_.flySpeed * (input.boost ? settings_.flySpeedBoost : 1.0f) * deltaTime;

        // target（= 回転中心）を直接動かす。distance はそのままなので、カメラは
        // 注視点との相対位置を保ったままワールド上を自由に平行移動する。
        state_.target = Vector::Add(state_.target, Vector::Multiply(speed, move));
        ClampTargetToWorldBounds();
    }

    void OrbitFlyController::UpdateSmoothMovement()
    {
        const float factor = settings_.smoothingFactor;

        smooth_.target.x += (state_.target.x - smooth_.target.x) * factor;
        smooth_.target.y += (state_.target.y - smooth_.target.y) * factor;
        smooth_.target.z += (state_.target.z - smooth_.target.z) * factor;

        smooth_.distance += (state_.distance - smooth_.distance) * factor;
        smooth_.pitch += (state_.pitch - smooth_.pitch) * factor;

        // ヨーは角度の周期性を考慮して補間する
        const float yawDiff = NormalizeAngle(state_.yaw - smooth_.yaw);
        smooth_.yaw = NormalizeAngle(smooth_.yaw + yawDiff * factor);
    }

    void OrbitFlyController::Reset()
    {
        state_ = OrbitState{};
        SyncSmoothToState();
        orbiting_ = false;
        panning_ = false;
        accumulatedWheelDelta_ = 0;
    }

    void OrbitFlyController::SetState(const OrbitState& state)
    {
        state_ = state;
        ClampTargetToWorldBounds();
        state_.distance = std::clamp(state_.distance, settings_.minDistance, settings_.maxDistance);
        SyncSmoothToState();
    }

    void OrbitFlyController::SetDistance(float distance)
    {
        state_.distance = std::clamp(distance, settings_.minDistance, settings_.maxDistance);
    }

    void OrbitFlyController::SyncSmoothToState()
    {
        smooth_ = state_;
    }

    void OrbitFlyController::ClampTargetToWorldBounds()
    {
        const float h = settings_.maxHorizontalExtent;
        state_.target.x = std::clamp(state_.target.x, -h, h);
        state_.target.z = std::clamp(state_.target.z, -h, h);
        state_.target.y = std::clamp(state_.target.y, settings_.minHeight, settings_.maxHeight);
    }

    void OrbitFlyController::ApplyPreset(Preset preset)
    {
        constexpr float kPi = std::numbers::pi_v<float>;
        state_.target = { 0.0f, 0.0f, 0.0f };

        switch (preset) {
        case Preset::Default:  state_.distance = 20.0f;  state_.pitch = 0.25f;        state_.yaw = kPi;        break;
        case Preset::Front:    state_.distance = 25.0f;  state_.pitch = 0.0f;         state_.yaw = kPi;        break;
        case Preset::Back:     state_.distance = 25.0f;  state_.pitch = 0.0f;         state_.yaw = 0.0f;       break;
        case Preset::Left:     state_.distance = 25.0f;  state_.pitch = 0.0f;         state_.yaw = kPi * 0.5f; break;
        case Preset::Right:    state_.distance = 25.0f;  state_.pitch = 0.0f;         state_.yaw = kPi * 1.5f; break;
        case Preset::Top:      state_.distance = 30.0f;  state_.pitch = kPi * 0.45f;  state_.yaw = kPi;        break;
        case Preset::Bottom:   state_.distance = 30.0f;  state_.pitch = -kPi * 0.45f; state_.yaw = kPi;        break;
        case Preset::Diagonal: state_.distance = 35.0f;  state_.pitch = 0.3f;         state_.yaw = kPi * 0.75f;break;
        case Preset::CloseUp:  state_.distance = 5.0f;   state_.pitch = 0.1f;         state_.yaw = kPi;        break;
        case Preset::Wide:     state_.distance = 100.0f; state_.pitch = 0.4f;         state_.yaw = kPi;        break;
        }

        SyncSmoothToState();
    }
}

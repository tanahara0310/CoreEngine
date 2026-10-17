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

        // ===== 2D 表示 =====
        constexpr float kView2DDistance = 100.0f;        // XY 面からカメラまでの距離（-Z 側）
        constexpr float kView2DZoomStep = 1.15f;         // ホイール 1 ノッチで変わる大きさの倍率
        constexpr float kMinOrthographicSize = 0.01f;
        constexpr float kMaxOrthographicSize = 100000.0f;
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
        if (view2D_.enabled) {
            // XY 面の手前（-Z）から +Z を向き、平行投影で映す
            camera.SetTranslate({ view2D_.center.x, view2D_.center.y, -kView2DDistance });
            camera.SetScale({ 1.0f, 1.0f, 1.0f });
            camera.SetRotate({ 0.0f, 0.0f, 0.0f });

            CameraParameters params = camera.GetParameters();
            params.projectionType = CameraProjectionType::Orthographic;
            params.orthographicSize = view2D_.orthographicSize;
            camera.SetParameters(params);
            return;
        }

        const OrbitState& s = settings_.smoothMovement ? smooth_ : state_;

        camera.SetTranslate(ComputeEyePosition());

        // 注視点を向く姿勢をオイラー角で表す。視点→注視点は outward の逆なので、
        // MakeAffine（Rx*Ry*Rz）の前方軸と一致させると pitch はそのまま、yaw は +π になる。
        // 結果は Matrix::LookAt(eye, target, +Y) と厳密に同じビュー行列。
        camera.SetScale({ 1.0f, 1.0f, 1.0f });
        camera.SetRotate({ s.pitch, MathCore::NormalizeAngle(s.yaw + std::numbers::pi_v<float>), 0.0f });
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
        } else if (view2D_.enabled) {
            HandleView2D(input, deltaTime);
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
            state_.yaw = MathCore::NormalizeAngle(state_.yaw + input.mouseDelta.x * settings_.rotationSensitivity);
            state_.pitch = std::clamp(state_.pitch + pitchDelta, -kMaxPitch, kMaxPitch);
        }

        if (panning_) {
            const Vector3 outward = {
                std::cosf(state_.pitch) * std::sinf(state_.yaw),
                std::sinf(state_.pitch),
                std::cosf(state_.pitch) * std::cosf(state_.yaw)
            };
            const Vector3 right = CoreEngine::Normalize(CoreEngine::Cross({ 0.0f, 1.0f, 0.0f }, outward));
            const Vector3 up = CoreEngine::Normalize(CoreEngine::Cross(outward, right));

            const float speed = settings_.panSensitivity;
            state_.target += right * (input.mouseDelta.x * speed);
            state_.target += up * (input.mouseDelta.y * speed);
            ClampTargetToWorldBounds();
        }
    }

    int OrbitFlyController::TakeWheelSteps(const CameraInputState& input)
    {
        if (input.wheelDelta == 0) {
            return 0;
        }

        accumulatedWheelDelta_ = std::clamp(
            accumulatedWheelDelta_ + input.wheelDelta, -kMaxAccumulated, kMaxAccumulated);

        if (std::abs(accumulatedWheelDelta_) < kWheelThreshold) {
            return 0;
        }

        const int wheelSteps = std::clamp(accumulatedWheelDelta_ / kWheelThreshold, -kMaxStepsPerFrame, kMaxStepsPerFrame);

        // 持ち越しで連続ジャンプしないよう取り出したらリセット
        accumulatedWheelDelta_ = 0;
        return wheelSteps;
    }

    void OrbitFlyController::HandleZoom(const CameraInputState& input)
    {
        const int wheelSteps = TakeWheelSteps(input);
        if (wheelSteps == 0) {
            return;
        }

        // distance を変えず target ごと前後させる。distance 比例のステップだと
        // 寄ると minDistance で頭打ち、遠ざかるとステップが際限なく大きくなる。
        const float dolly = static_cast<float>(wheelSteps) * settings_.zoomSensitivity;
        const Vector3 forward = {
            -std::cosf(state_.pitch) * std::sinf(state_.yaw),
            -std::sinf(state_.pitch),
            -std::cosf(state_.pitch) * std::cosf(state_.yaw)
        };
        state_.target += forward * dolly;
        ClampTargetToWorldBounds();
    }

    void OrbitFlyController::HandleView2D(const CameraInputState& input, float deltaTime)
    {
        // 中ドラッグは Shift の有無にかかわらず移動にする（回転はしない）
        const bool dragEdge = input.orbitButton && !prevOrbitButton_;
        prevOrbitButton_ = input.orbitButton;
        if (dragEdge) {
            panning_ = true;
            orbiting_ = false;
        }
        if (!input.orbitButton) {
            panning_ = false;
        }

        const bool hasViewport = input.viewportSize.x > 0.0f && input.viewportSize.y > 0.0f;

        // 1 ピクセルが映すワールドの長さ。表示の大きさが分からないときは 3D のパン感度を使う
        const float unitsPerPixel = hasViewport
            ? view2D_.orthographicSize * 2.0f / input.viewportSize.y
            : settings_.panSensitivity;

        if (panning_) {
            // つかんだ点がカーソルに付いてくるよう、カーソルと逆へ動かす
            view2D_.center.x -= input.mouseDelta.x * unitsPerPixel;
            view2D_.center.y += input.mouseDelta.y * unitsPerPixel;
        }

        // ホイールは大きさを変える。カーソルの下に映っている点は動かさない
        if (const int wheelSteps = TakeWheelSteps(input); wheelSteps != 0) {
            const float oldSize = view2D_.orthographicSize;
            const float newSize = std::clamp(oldSize / std::powf(kView2DZoomStep, static_cast<float>(wheelSteps)),
                kMinOrthographicSize, kMaxOrthographicSize);
            if (hasViewport) {
                const float aspect = input.viewportSize.x / input.viewportSize.y;
                view2D_.center.x += input.cursorNdc.x * (oldSize - newSize) * aspect;
                view2D_.center.y += input.cursorNdc.y * (oldSize - newSize);
            }
            view2D_.orthographicSize = newSize;
        }

        // 移動キーは XY 面の上下左右
        Vector2 move = { 0.0f, 0.0f };
        if (input.forward || input.up)  move.y += 1.0f;
        if (input.back || input.down)   move.y -= 1.0f;
        if (input.right)                move.x += 1.0f;
        if (input.left)                 move.x -= 1.0f;
        if (move.x != 0.0f || move.y != 0.0f) {
            const float length = std::sqrt(move.x * move.x + move.y * move.y);
            const float speed = settings_.flySpeed * (input.boost ? settings_.flySpeedBoost : 1.0f) * deltaTime;
            view2D_.center.x += move.x / length * speed;
            view2D_.center.y += move.y / length * speed;
        }

        ClampView2D();
    }

    void OrbitFlyController::HandleFlyMove(const CameraInputState& input, float deltaTime)
    {
        const Vector3 outward = {
            std::cosf(state_.pitch) * std::sinf(state_.yaw),
            std::sinf(state_.pitch),
            std::cosf(state_.pitch) * std::cosf(state_.yaw)
        };
        const Vector3 forward = -outward;
        const Vector3 right = Normalize(Cross({ 0.0f, 1.0f, 0.0f }, outward));
        const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };

        Vector3 move = { 0.0f, 0.0f, 0.0f };
        if (input.forward) move += forward;
        if (input.back)    move -= forward;
        if (input.right)   move += right;
        if (input.left)    move -= right;
        if (input.up)      move += worldUp;
        if (input.down)    move -= worldUp;

        if (LengthSquared(move) <= 1e-10f) {
            return;
        }
        move = Normalize(move);

        const float speed = settings_.flySpeed * (input.boost ? settings_.flySpeedBoost : 1.0f) * deltaTime;

        // target（= 回転中心）を直接動かす。distance はそのままなので、カメラは
        // 注視点との相対位置を保ったままワールド上を自由に平行移動する。
        state_.target += move * speed;
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
        const float yawDiff = MathCore::NormalizeAngle(state_.yaw - smooth_.yaw);
        smooth_.yaw = MathCore::NormalizeAngle(smooth_.yaw + yawDiff * factor);
    }

    void OrbitFlyController::Reset()
    {
        state_ = OrbitState{};
        SyncSmoothToState();

        // 2D 表示かどうかは保ったまま、中心と大きさを戻す
        const bool enabled2D = view2D_.enabled;
        view2D_ = View2DState{};
        view2D_.enabled = enabled2D;

        orbiting_ = false;
        panning_ = false;
        accumulatedWheelDelta_ = 0;
    }

    void OrbitFlyController::SetView2DEnabled(bool enabled, Camera& camera)
    {
        if (enabled == view2D_.enabled) {
            return;
        }

        if (enabled) {
            view2D_.center = { state_.target.x, state_.target.y };
        } else {
            state_.target.x = view2D_.center.x;
            state_.target.y = view2D_.center.y;
            ClampTargetToWorldBounds();
            SyncSmoothToState();

            CameraParameters params = camera.GetParameters();
            params.projectionType = CameraProjectionType::Perspective;
            camera.SetParameters(params);
        }

        view2D_.enabled = enabled;
        ClampView2D();
        orbiting_ = false;
        panning_ = false;
        accumulatedWheelDelta_ = 0;
        ApplyTo(camera);
    }

    void OrbitFlyController::SetView2DState(const View2DState& state)
    {
        view2D_ = state;
        ClampView2D();
    }

    void OrbitFlyController::ClampView2D()
    {
        const float h = settings_.maxHorizontalExtent;
        view2D_.center.x = std::clamp(view2D_.center.x, -h, h);
        view2D_.center.y = std::clamp(view2D_.center.y, -h, h);
        view2D_.orthographicSize = std::clamp(view2D_.orthographicSize, kMinOrthographicSize, kMaxOrthographicSize);
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

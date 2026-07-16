#include "pch.h"
#include "DebugCamera.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "WinApp/WinApp.h"

// 新しい数学
using namespace CoreEngine::MathCore;



namespace CoreEngine
{
    DebugCamera::DebugCamera()
        : distance_(20.0f)
        , pitch_(0.25f)
        , yaw_(std::numbers::pi_v<float>)
        , target_{ 0.0f, 0.0f, 0.0f }
        , viewMatrix_(Matrix::Identity())
        , projectionMatrix_(Matrix::Identity())
        , cameraGPUResource_(nullptr)
        , cameraGPUData_(nullptr)
        , draggingLeft_(false)
        , draggingMiddle_(false)
        , settings_{}
        , parameters_{}  // デフォルトパラメータ
        , targetSmooth_{ 0.0f, 0.0f, 0.0f }
        , distanceSmooth_(20.0f)
        , pitchSmooth_(0.25f)
        , yawSmooth_(std::numbers::pi_v<float>)
        , engineSystem_(nullptr)
        , mouseState_{}
    {
        // プロジェクション行列をパラメータから初期化
        float aspectRatio = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
            static_cast<float>(WinApp::GetCurrentClientHeightStatic());
        projectionMatrix_ = Rendering::PerspectiveFov(
            parameters_.fov,
            aspectRatio,
            parameters_.nearClip,
            parameters_.farClip
        );
    }

    void DebugCamera::Initialize(EngineSystem* engine, ID3D12Device* device)
    {
        engineSystem_ = engine;

        // CameraForGPU用の定数バッファを初期化
        if (device) {
            cameraGPUResource_ = ResourceFactory::CreateBufferResource(device, sizeof(CameraForGPU));
            // マッピングしてデータを書き込む
            cameraGPUResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraGPUData_));
        }

        // ビュー行列を初期化
        UpdateMatrices();
        // GPU に転送
        TransferMatrix();
    }

    void DebugCamera::Reset()
    {
        distance_ = 20.0f;
        pitch_ = 0.25f;
        yaw_ = std::numbers::pi_v<float>;
        target_ = { 0.0f, 0.0f, 0.0f };

        // スムーズ移動用パラメータもリセット
        targetSmooth_ = target_;
        distanceSmooth_ = distance_;
        pitchSmooth_ = pitch_;
        yawSmooth_ = yaw_;

        // マウス状態もリセット
        mouseState_ = MouseState{};

        // 行列を更新
        UpdateMatrices();
        // GPU に転送
        TransferMatrix();
    }

    Vector3 DebugCamera::GetPosition() const
    {
        // ビュー差し替え（BeginViewOverride）中は差し替え後の視点を返す
        if (viewOverrideActive_) {
            return overrideViewPosition_;
        }

        // スムーズ移動が有効な場合はスムーズ値を使用
        if (settings_.smoothMovement) {
            return {
                targetSmooth_.x + distanceSmooth_ * cosf(pitchSmooth_) * sinf(yawSmooth_),
                targetSmooth_.y + distanceSmooth_ * sinf(pitchSmooth_),
                targetSmooth_.z + distanceSmooth_ * cosf(pitchSmooth_) * cosf(yawSmooth_)
            };
        } else {
            return {
                target_.x + distance_ * cosf(pitch_) * sinf(yaw_),
                target_.y + distance_ * sinf(pitch_),
                target_.z + distance_ * cosf(pitch_) * cosf(yaw_)
            };
        }
    }

    void DebugCamera::UpdateMatrices()
    {
        Vector3 eye = GetPosition();
        Vector3 targetPos = settings_.smoothMovement ? targetSmooth_ : target_;
        Vector3 up = (cosf(settings_.smoothMovement ? pitchSmooth_ : pitch_) >= 0.0f)
            ? Vector3{ 0.0f, 1.0f, 0.0f }
        : Vector3{ 0.0f, -1.0f, 0.0f };

        viewMatrix_ = Matrix::LookAt(eye, targetPos, up);

        // プロジェクション行列をパラメータから更新
        float aspectRatio = parameters_.aspectRatio;
        if (aspectRatio <= 0.0f) {
            aspectRatio = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
                static_cast<float>(WinApp::GetCurrentClientHeightStatic());
        }

        projectionMatrix_ = Rendering::PerspectiveFov(
            parameters_.fov,
            aspectRatio,
            parameters_.nearClip,
            parameters_.farClip
        );
    }

    void DebugCamera::TransferMatrix()
    {
        // カメラ座標の転送（CameraForGPU）
        // GetPosition() がビュー差し替え（BeginViewOverride）を考慮する
        if (cameraGPUData_) {
            cameraGPUData_->worldPosition = GetPosition();
        }
    }

    bool DebugCamera::BeginViewOverride(
        const Matrix4x4& viewMatrix,
        const Vector3& viewPosition,
        const Matrix4x4* projectionOverride)
    {
        // 差し替え中も distance_ / target_ 等の操作状態には一切触れない。
        // TransferMatrix()（GPU 定数バッファへの書き込み）もここでは行わない。
        // 定数バッファは単一のアップロードバッファであり、フレーム内で
        // 「差し替え→復元」と 2 回書き換えると実行中の前フレーム（インフライト）が
        // どちらを読むかタイミング依存になり、カメラ位置依存のフレネル項が
        // 毎フレームちらつく。差し替えは CPU 側 WVP 計算経路にのみ作用させる。
        viewOverrideActive_ = true;
        overrideViewMatrix_ = viewMatrix;
        overrideViewPosition_ = viewPosition;
        projectionOverrideActive_ = (projectionOverride != nullptr);
        if (projectionOverride) {
            overrideProjectionMatrix_ = *projectionOverride;
        }
        return true;
    }

    void DebugCamera::EndViewOverride()
    {
        viewOverrideActive_ = false;
        projectionOverrideActive_ = false;
    }

    void DebugCamera::ApplyPreset(CameraPreset preset)
    {
        switch (preset) {
        case CameraPreset::Default:
            distance_ = 20.0f;
            pitch_ = 0.25f;
            yaw_ = std::numbers::pi_v<float>;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Front:
            distance_ = 25.0f;
            pitch_ = 0.0f;
            yaw_ = std::numbers::pi_v<float>;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Back:
            distance_ = 25.0f;
            pitch_ = 0.0f;
            yaw_ = 0.0f;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Left:
            distance_ = 25.0f;
            pitch_ = 0.0f;
            yaw_ = std::numbers::pi_v<float> *0.5f;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Right:
            distance_ = 25.0f;
            pitch_ = 0.0f;
            yaw_ = std::numbers::pi_v<float> *1.5f;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Top:
            distance_ = 30.0f;
            pitch_ = std::numbers::pi_v<float> *0.45f;
            yaw_ = std::numbers::pi_v<float>;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Bottom:
            distance_ = 30.0f;
            pitch_ = -std::numbers::pi_v<float> *0.45f;
            yaw_ = std::numbers::pi_v<float>;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Diagonal:
            distance_ = 35.0f;
            pitch_ = 0.3f;
            yaw_ = std::numbers::pi_v<float> *0.75f;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::CloseUp:
            distance_ = 5.0f;
            pitch_ = 0.1f;
            yaw_ = std::numbers::pi_v<float>;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;

        case CameraPreset::Wide:
            distance_ = 100.0f;
            pitch_ = 0.4f;
            yaw_ = std::numbers::pi_v<float>;
            target_ = { 0.0f, 0.0f, 0.0f };
            break;
        }

        // プリセット適用後に行列を更新
        UpdateMatrices();
    }

    void DebugCamera::SetDistance(float distance)
    {
        distance_ = std::clamp(distance, settings_.minDistance, settings_.maxDistance);
    }

    void DebugCamera::Update()
    {
#ifdef USE_IMGUI
        HandleMouseInput();

        float deltaTime = 1.0f / 60.0f;
        if (engineSystem_) {
            if (auto* frameRate = engineSystem_->GetComponent<FrameRateController>()) {
                deltaTime = frameRate->GetDeltaTime();
            }
        }
        HandleKeyboardInput(deltaTime);

        if (settings_.smoothMovement) {
            UpdateSmoothMovement();
        }

        // 行列を更新
        UpdateMatrices();

        // GPU に転送
        TransferMatrix();
#endif
    }

#ifdef USE_IMGUI
    void DebugCamera::HandleMouseInput()
    {
        // エンジンシステムが無効な場合は処理しない
        if (!engineSystem_) {
            return;
        }

        // === ギズモ操作中はカメラ操作を無効化 ===
        bool gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        if (gizmoActive) {
            draggingLeft_ = false;
            draggingMiddle_ = false;
            return;
        }

        // === ドッキングのリサイズ中は操作を無効化 ===
        ImGuiMouseCursor mc = ImGui::GetMouseCursor();
        if (mc == ImGuiMouseCursor_ResizeEW
            || mc == ImGuiMouseCursor_ResizeNS
            || mc == ImGuiMouseCursor_Hand
            || ImGui::IsDragDropActive()) {
            draggingLeft_ = false;
            draggingMiddle_ = false;
            return;
        }

        // InputQueryを取得
        auto inputManager = engineSystem_->GetComponent<InputManager>();
        if (!inputManager) {
            return;
        }
        auto& input = inputManager->GetQuery();

        // シーンウィンドウ内での操作かを判定
        bool isInSceneWindow = IsMouseInSceneWindow();

        // Shiftキーの状態を取得
        bool isShiftPressed = input.IsKeyPressed(DIK_LSHIFT) || input.IsKeyPressed(DIK_RSHIFT);

        // === 中ボタンによる操作 ===
        bool middlePressed = input.IsMouseButtonPressed(MouseButton::Middle);
        bool middleTriggered = input.IsMouseButtonTriggered(MouseButton::Middle);

        if (middleTriggered && isInSceneWindow) {
            if (isShiftPressed) {
                draggingMiddle_ = true; // Shift + 中ボタン = パン操作
                draggingLeft_ = false;
            } else {
                draggingLeft_ = true;   // 中ボタンのみ = 回転操作
                draggingMiddle_ = false;
            }
            POINT pos = input.GetCursorPosition();
            mouseState_.lastX = static_cast<float>(pos.x);
            mouseState_.lastY = static_cast<float>(pos.y);
        }

        if (!middlePressed || !isInSceneWindow) {
            draggingLeft_ = false;
            draggingMiddle_ = false;
        }

        // === 回転操作（中ボタンドラッグ） ===
        if (draggingLeft_) {
            POINT currentPos = input.GetCursorPosition();
            float currentX = static_cast<float>(currentPos.x);
            float currentY = static_cast<float>(currentPos.y);

            float deltaX = currentX - mouseState_.lastX;
            float deltaY = currentY - mouseState_.lastY;

            float yawDelta = deltaX * settings_.rotationSensitivity;
            float pitchDelta = deltaY * settings_.rotationSensitivity;

            // Y軸反転の処理
            if (settings_.invertY) {
                pitchDelta = -pitchDelta;
            }

            yaw_ += yawDelta;
            pitch_ += pitchDelta;

            // ピッチの制限（真上・真下を少し手前で制限）
            const float maxPitch = std::numbers::pi_v<float> *0.49f; // 88.2度
            pitch_ = std::clamp(pitch_, -maxPitch, maxPitch);

            // ヨーの正規化
            yaw_ = NormalizeAngle(yaw_);

            // 現在位置を更新
            mouseState_.lastX = currentX;
            mouseState_.lastY = currentY;
        }

        // === パン操作（Shift + 中ボタンドラッグ） ===
        if (draggingMiddle_) {
            POINT currentPos = input.GetCursorPosition();
            float currentX = static_cast<float>(currentPos.x);
            float currentY = static_cast<float>(currentPos.y);

            float deltaX = currentX - mouseState_.lastX;
            float deltaY = currentY - mouseState_.lastY;

            Vector3 forward = {
                cosf(pitch_) * sinf(yaw_),
                sinf(pitch_),
                cosf(pitch_) * cosf(yaw_)
            };

            Vector3 right = Vector::Normalize(Vector::Cross({ 0.0f, 1.0f, 0.0f }, forward));
            Vector3 up = Vector::Normalize(Vector::Cross(forward, right));

            // 固定速度（distance_ に比例させない）。以前は distance_ * panSensitivity だったため、
            // 注視点に近づく（distance_ が小さくなる）ほどパンが遅くなり自由に動けなかった。
            float speed = settings_.panSensitivity;

            target_ = Vector::Add(target_, Vector::Multiply(deltaX * speed, right));
            target_ = Vector::Add(target_, Vector::Multiply(deltaY * speed, up));
            ClampTargetToWorldBounds();

            // 現在位置を更新
            mouseState_.lastX = currentX;
            mouseState_.lastY = currentY;
        }

        // === ホイールによるズーム ===
        int wheelDelta = input.GetWheelDelta();
        if (!isInSceneWindow) {
            // シーン外で溜まった値を持ち越さない
            mouseState_.accumulatedWheelDelta = 0;
        }

        if (isInSceneWindow && wheelDelta != 0) {
            // ホイールの値をフレーム単位で累積
            mouseState_.accumulatedWheelDelta += wheelDelta;

            // 異常に大きい入力はクリップ
            const int maxAccumulated = 1200; // 10ノッチ分
            mouseState_.accumulatedWheelDelta = std::clamp(
                mouseState_.accumulatedWheelDelta,
                -maxAccumulated,
                maxAccumulated);

            // 一定以上累積されたらズーム処理
            const int wheelThreshold = 120; // DirectInputの標準的なホイール1クリック分
            if (std::abs(mouseState_.accumulatedWheelDelta) >= wheelThreshold) {
                int wheelSteps = mouseState_.accumulatedWheelDelta / wheelThreshold;
                wheelSteps = std::clamp(wheelSteps, -3, 3); // 1フレームあたり最大3ノッチ

                // ズーム = 視線方向への一定量ドリー移動。注視点(target_)ごと前後に動かす。
                // 以前は distance_（軌道半径）を距離比例のステップで増減していたため、
                //  ・寄ると minDistance で頭打ちになり注視点より内側へ寄れない
                //  ・遠ざかるとステップ幅が際限なく大きくなり収拾がつかない
                // という問題があった。distance_ を変えず target_ を一定量動かすことで、
                // どの位置でも同じ量・上限下限なくズームできるようにする。
                // wheelSteps > 0（手前へ回す）＝前進＝ズームイン。
                float dolly = static_cast<float>(wheelSteps) * settings_.zoomSensitivity;

                // カメラが向いている方向（注視点→シーン奥）。GetPosition の outward の逆。
                Vector3 forward = {
                    -cosf(pitch_) * sinf(yaw_),
                    -sinf(pitch_),
                    -cosf(pitch_) * cosf(yaw_)
                };
                target_ = Vector::Add(target_, Vector::Multiply(dolly, forward));
                ClampTargetToWorldBounds();

                // 持ち越しで連続ジャンプしないよう処理後はリセット
                mouseState_.accumulatedWheelDelta = 0;
            }
        }
    }

    void DebugCamera::HandleKeyboardInput(float deltaTime)
    {
        if (!engineSystem_) {
            return;
        }

        // テキスト入力等、ImGui がキーボードを掴んでいる間は奪わない
        if (ImGui::GetIO().WantCaptureKeyboard) {
            return;
        }

        // ギズモ操作中は無効化（マウス操作と同じガード）
        if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) {
            return;
        }

        // シーン/ゲームビューをホバー中のみ有効（他の操作中の誤動作防止）
        if (!IsMouseInSceneWindow()) {
            return;
        }

        auto inputManager = engineSystem_->GetComponent<InputManager>();
        if (!inputManager) {
            return;
        }
        auto& input = inputManager->GetQuery();

        // 視線方向の基底ベクトル。GetPosition() の式（target + distance*outward）における
        // outward（注視点→カメラ方向）の符号を反転したものが「カメラが向いている方向」。
        Vector3 outward = {
            cosf(pitch_) * sinf(yaw_),
            sinf(pitch_),
            cosf(pitch_) * cosf(yaw_)
        };
        Vector3 forward = Vector::Multiply(-1.0f, outward);
        Vector3 right = Vector::Normalize(Vector::Cross({ 0.0f, 1.0f, 0.0f }, outward));
        Vector3 up = { 0.0f, 1.0f, 0.0f };

        Vector3 move = { 0.0f, 0.0f, 0.0f };
        if (input.IsKeyPressed(DIK_W))                                move = Vector::Add(move, forward);
        if (input.IsKeyPressed(DIK_S))                                move = Vector::Add(move, Vector::Multiply(-1.0f, forward));
        if (input.IsKeyPressed(DIK_D))                                move = Vector::Add(move, right);
        if (input.IsKeyPressed(DIK_A))                                move = Vector::Add(move, Vector::Multiply(-1.0f, right));
        if (input.IsKeyPressed(DIK_E) || input.IsKeyPressed(DIK_SPACE))    move = Vector::Add(move, up);
        if (input.IsKeyPressed(DIK_Q) || input.IsKeyPressed(DIK_LCONTROL)) move = Vector::Add(move, Vector::Multiply(-1.0f, up));

        const float moveLenSq = move.x * move.x + move.y * move.y + move.z * move.z;
        if (moveLenSq <= 1e-10f) {
            return;
        }
        move = Vector::Normalize(move);

        bool boost = input.IsKeyPressed(DIK_LSHIFT) || input.IsKeyPressed(DIK_RSHIFT);
        float speed = settings_.flySpeed * (boost ? settings_.flySpeedBoost : 1.0f) * deltaTime;

        // target_（＝回転中心）を直接動かす。distance_ はそのままなので、
        // カメラは注視点との相対位置を保ったままワールド上を自由に平行移動する。
        target_ = Vector::Add(target_, Vector::Multiply(speed, move));
        ClampTargetToWorldBounds();
    }

    bool DebugCamera::IsMouseInSceneWindow() const
    {
        // useGameView が有効な場合は "Game" ウィンドウ、無効な場合は "Scene" ウィンドウを判定する
        const char* targetWindowName = settings_.useGameView ? "Game" : "Scene";
        ImGuiWindow* targetWin = ImGui::FindWindowByName(targetWindowName);
        if (!targetWin) {
            return false;
        }

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mousePos = io.MousePos;
        ImVec2 pos = targetWin->Pos;
        ImVec2 size = targetWin->Size;

        return mousePos.x >= pos.x && mousePos.x <= pos.x + size.x &&
            mousePos.y >= pos.y && mousePos.y <= pos.y + size.y;
    }

    void DebugCamera::UpdateSmoothMovement()
    {
        if (!settings_.smoothMovement) return;

        float factor = settings_.smoothingFactor;

        // 線形補間でスムーズに移動
        targetSmooth_.x = targetSmooth_.x + (target_.x - targetSmooth_.x) * factor;
        targetSmooth_.y = targetSmooth_.y + (target_.y - targetSmooth_.y) * factor;
        targetSmooth_.z = targetSmooth_.z + (target_.z - targetSmooth_.z) * factor;

        distanceSmooth_ = distanceSmooth_ + (distance_ - distanceSmooth_) * factor;
        pitchSmooth_ = pitchSmooth_ + (pitch_ - pitchSmooth_) * factor;

        // ヨー角は角度の特性を考慮して補間
        float yawDiff = yaw_ - yawSmooth_;
        yawDiff = NormalizeAngle(yawDiff);
        yawSmooth_ += yawDiff * factor;
        yawSmooth_ = NormalizeAngle(yawSmooth_);
    }
#endif

    void DebugCamera::ClampTargetToWorldBounds()
    {
        // 水平(X/Z)は原点からの各軸絶対値、高度(Y)は独立した上下限でクランプする。
        const float h = settings_.maxHorizontalExtent;
        target_.x = std::clamp(target_.x, -h, h);
        target_.z = std::clamp(target_.z, -h, h);
        target_.y = std::clamp(target_.y, settings_.minHeight, settings_.maxHeight);
    }

    const char* DebugCamera::GetPresetName(CameraPreset preset) const
    {
        switch (preset) {
        case CameraPreset::Default:  return "デフォルト";
        case CameraPreset::Front:return "正面";
        case CameraPreset::Back:     return "背面";
        case CameraPreset::Left:     return "左側";
        case CameraPreset::Right:    return "右側";
        case CameraPreset::Top:      return "上から";
        case CameraPreset::Bottom:   return "下から";
        case CameraPreset::Diagonal: return "斜め";
        case CameraPreset::CloseUp:  return "接近";
        case CameraPreset::Wide:     return "広角";
        default:       return "不明";
        }
    }

    float DebugCamera::NormalizeAngle(float angle) const
    {
        while (angle > std::numbers::pi_v<float>) {
            angle -= 2.0f * std::numbers::pi_v<float>;
        }
        while (angle < -std::numbers::pi_v<float>) {
            angle += 2.0f * std::numbers::pi_v<float>;
        }
        return angle;
    }

    // 現在の状態をスナップショットとして保存
    CameraSnapshot DebugCamera::CaptureSnapshot(const std::string& name) const
    {
        CameraSnapshot snapshot;
        snapshot.name = name;
        snapshot.isDebugCamera = true;

        // DebugCamera用パラメータ
        snapshot.target = target_;
        snapshot.distance = distance_;
        snapshot.pitch = pitch_;
        snapshot.yaw = yaw_;

        // カメラパラメータ
        snapshot.parameters = parameters_;

        return snapshot;
    }

    // スナップショットから状態を復元
    void DebugCamera::RestoreSnapshot(const CameraSnapshot& snapshot)
    {
        // ReleaseCamera用のスナップショットは無視
        if (!snapshot.isDebugCamera) {
            return;
        }

        // DebugCamera用パラメータを復元（範囲外の値を持つスナップショットに備えクランプする）
        target_ = snapshot.target;
        ClampTargetToWorldBounds();
        distance_ = std::clamp(snapshot.distance, settings_.minDistance, settings_.maxDistance);
        pitch_ = snapshot.pitch;
        yaw_ = snapshot.yaw;

        // スムーズ移動用の値も同期
        targetSmooth_ = target_;
        distanceSmooth_ = distance_;
        pitchSmooth_ = pitch_;
        yawSmooth_ = yaw_;

        // カメラパラメータを復元
        parameters_ = snapshot.parameters;

        // 行列を更新
        UpdateMatrices();
    }
}

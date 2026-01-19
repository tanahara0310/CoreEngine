#include "DebugCamera.h"
#include <EngineSystem.h>
#include "Graphics/Resource/ResourceFactory.h"

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
		float aspectRatio = static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight);
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
			aspectRatio = static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight);
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
		if (cameraGPUData_) {
			cameraGPUData_->worldPosition = GetPosition();
		}
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
#ifdef _DEBUG
		HandleMouseInput();

		if (settings_.smoothMovement) {
			UpdateSmoothMovement();
		}

		// 行列を更新
		UpdateMatrices();

		// GPU に転送
		TransferMatrix();
#endif
	}

#ifdef _DEBUG
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

		// 新方式でコンポーネントを直接取得
		auto mouse = engineSystem_->GetComponent<MouseInput>();
		auto keyboard = engineSystem_->GetComponent<KeyboardInput>();
		if (!mouse || !keyboard) {
			return;
		}

		// シーンウィンドウ内での操作かを判定
		bool isInSceneWindow = IsMouseInSceneWindow();

		// Shiftキーの状態を取得
		bool isShiftPressed = keyboard->IsKeyPressed(DIK_LSHIFT) || keyboard->IsKeyPressed(DIK_RSHIFT);

		// === 中ボタンによる操作 ===
		bool middlePressed = mouse->IsButtonPressed(MouseButton::Middle);
		bool middleTriggered = mouse->IsButtonTriggered(MouseButton::Middle);

		if (middleTriggered && isInSceneWindow) {
			if (isShiftPressed) {
				draggingMiddle_ = true; // Shift + 中ボタン = パン操作
				draggingLeft_ = false;
			} else {
				draggingLeft_ = true;   // 中ボタンのみ = 回転操作
				draggingMiddle_ = false;
			}
			POINT pos = mouse->GetCursorPosition();
			mouseState_.lastX = static_cast<float>(pos.x);
			mouseState_.lastY = static_cast<float>(pos.y);
		}

		if (!middlePressed || !isInSceneWindow) {
			draggingLeft_ = false;
			draggingMiddle_ = false;
		}

		// === 回転操作（中ボタンドラッグ） ===
		if (draggingLeft_) {
			POINT currentPos = mouse->GetCursorPosition();
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
			POINT currentPos = mouse->GetCursorPosition();
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

			float speed = distance_ * settings_.panSensitivity;

			target_ = Vector::Add(target_, Vector::Multiply(deltaX * speed, right));
			target_ = Vector::Add(target_, Vector::Multiply(deltaY * speed, up));

			// 現在位置を更新
			mouseState_.lastX = currentX;
			mouseState_.lastY = currentY;
		}

		// === ホイールによるズーム ===
		int wheelDelta = mouse->GetWheelDelta();
		if (isInSceneWindow && wheelDelta != 0) {
			// ホイールの値をフレーム単位で累積
			mouseState_.accumulatedWheelDelta += wheelDelta;

			// 一定以上累積されたらズーム処理
			const int wheelThreshold = 120; // DirectInputの標準的なホイール1クリック分
			if (abs(mouseState_.accumulatedWheelDelta) >= wheelThreshold) {
				float zoomDelta = static_cast<float>(-mouseState_.accumulatedWheelDelta) / wheelThreshold * settings_.zoomSensitivity;

				// 距離に応じてズーム量を調整（近い時は細かく、遠い時は大きく）
				float adaptiveZoom = (0.1f > distance_ * 0.1f) ? 0.1f : distance_ * 0.1f;
				zoomDelta *= adaptiveZoom;

				SetDistance(distance_ + zoomDelta);

				// 処理した分だけ累積値を減らす
				int processedDelta = (mouseState_.accumulatedWheelDelta / wheelThreshold) * wheelThreshold;
				mouseState_.accumulatedWheelDelta -= processedDelta;
			}
		}
	}

	bool DebugCamera::IsMouseInSceneWindow() const
	{
		// Scene ウィンドウのホバー判定
		ImGuiWindow* sceneWin = ImGui::FindWindowByName("Scene");
		if (!sceneWin) {
			return false;
		}

		ImGuiIO& io = ImGui::GetIO();
		ImVec2 mousePos = io.MousePos;
		ImVec2 pos = sceneWin->Pos;
		ImVec2 size = sceneWin->Size;

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

		// DebugCamera用パラメータを復元
		target_ = snapshot.target;
		distance_ = snapshot.distance;
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

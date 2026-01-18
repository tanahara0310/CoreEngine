#include "ReticleObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyboardInput.h"
#include "Engine/Input/GamepadInput.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/WinApp/WinApp.h"
#include "Engine/Math/MathCore.h"
#include <dinput.h>
#include <algorithm>

void ReticleObject::Initialize(const std::string& textureFilePath) {
	// スプライトオブジェクトを作成
	sprite_ = std::make_unique<CoreEngine::SpriteObject>();
	sprite_->Initialize(textureFilePath);
	sprite_->GetSpriteTransform().translate = { 0.0f, 0.0f, 0.0f };  // 画面中央
	sprite_->GetSpriteTransform().scale = { 0.5f, 0.5f, 0.5f };
	sprite_->SetActive(true);

	// ブレンドモードを明示的に設定（透明部分が正しく描画されるように）
	sprite_->SetBlendMode(CoreEngine::BlendMode::kBlendModeNormal);

	// 初期位置を正規化座標に設定
	normalizedPosition_ = { 0.0f, 0.0f };

	// アクティブ状態に設定
	SetActive(true);
}

void ReticleObject::Update() {
	if (!IsActive() || !sprite_) {
		return;
	}

	// スプライトが非表示の場合は更新をスキップ
	if (!sprite_->IsActive()) {
		return;
	}

	auto engine = GetEngineSystem();
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) {
		return;
	}

	float deltaTime = frameRateController->GetDeltaTime();

	// 入力処理
	ProcessInput(deltaTime);

	// 回転を更新
	UpdateRotation(deltaTime);

	// スプライトの更新
	sprite_->Update();

	// 正規化座標を常に更新（初期化後も確実に更新されるように）
	UpdateNormalizedPosition();
}

void ReticleObject::Draw(const CoreEngine::ICamera* camera) {
	if (!camera || !sprite_) {
		return;
	}

	// スプライトが非表示の場合は描画をスキップ
	if (!sprite_->IsActive()) {
		return;
	}

	// スプライトの描画
	sprite_->Draw(camera);
}

void ReticleObject::ProcessInput(float deltaTime) {
	// 移動方向を計算
	CoreEngine::Vector2 moveDirection = CalculateMoveDirection();

	// レティクルを移動
	if (moveDirection.x != 0.0f || moveDirection.y != 0.0f) {
		MoveReticle(moveDirection, deltaTime);
	}
}

CoreEngine::Vector2 ReticleObject::CalculateMoveDirection() const {
	auto engine = GetEngineSystem();
	auto inputManager = engine->GetComponent<CoreEngine::InputManager>();
	if (!inputManager) return { 0.0f, 0.0f };

	CoreEngine::Vector2 direction = { 0.0f, 0.0f };

	// キーボード入力（矢印キー）
	auto keyboard = inputManager->GetKeyboard();
	if (keyboard) {
		if (keyboard->IsKeyPressed(DIK_UP)) {
			direction.y += 1.0f;
		}
		if (keyboard->IsKeyPressed(DIK_DOWN)) {
			direction.y -= 1.0f;
		}
		if (keyboard->IsKeyPressed(DIK_LEFT)) {
			direction.x -= 1.0f;
		}
		if (keyboard->IsKeyPressed(DIK_RIGHT)) {
			direction.x += 1.0f;
		}
	}

	// ゲームパッド入力（右スティック）
	auto gamepad = inputManager->GetGamepad();
	if (gamepad && gamepad->IsConnected()) {
		CoreEngine::Stick rightStick = gamepad->GetRightStick();

		// 右スティック入力を加算（キーボード入力と合算）
		direction.x += rightStick.x;
		direction.y += rightStick.y;  // Y軸を上下方向に
	}

	// 正規化（長さを1にする）
	float length = std::sqrtf(direction.x * direction.x + direction.y * direction.y);
	if (length > 0.0f) {
		direction.x /= length;
		direction.y /= length;
	}

	return direction;
}

void ReticleObject::MoveReticle(const CoreEngine::Vector2& direction, float deltaTime) {
	if (!sprite_) return;

	// 移動量を計算（ピクセル単位）
	CoreEngine::Vector2 velocity = {
		direction.x * moveSpeed_ * deltaTime,
		direction.y * moveSpeed_ * deltaTime
	};

	// 現在の座標を取得
	CoreEngine::Vector3& spritePos = sprite_->GetSpriteTransform().translate;

	// 座標を更新
	spritePos.x += velocity.x;
	spritePos.y += velocity.y;

	// 画面端での制限（ウィンドウサイズを使用）
	float halfWidth = static_cast<float>(CoreEngine::WinApp::kClientWidth) / 2.0f;
	float halfHeight = static_cast<float>(CoreEngine::WinApp::kClientHeight) / 2.0f;

	// 移動範囲を制限（余白を考慮）
	float minX = -halfWidth + marginX_;
	float maxX = halfWidth - marginX_;
	float minY = -halfHeight + marginY_;
	float maxY = halfHeight - marginY_;

	spritePos.x = std::clamp(spritePos.x, minX, maxX);
	spritePos.y = std::clamp(spritePos.y, minY, maxY);

	// 正規化座標を更新
	UpdateNormalizedPosition();
}

void ReticleObject::UpdateNormalizedPosition() {
	if (!sprite_) return;

	// スプライトの座標を取得
	const CoreEngine::Vector3& spritePos = sprite_->GetSpriteTransform().translate;

	// 画面サイズの半分
	float halfWidth = static_cast<float>(CoreEngine::WinApp::kClientWidth) / 2.0f;
	float halfHeight = static_cast<float>(CoreEngine::WinApp::kClientHeight) / 2.0f;

	// 正規化座標に変換（-1.0～1.0の範囲）
	normalizedPosition_.x = spritePos.x / halfWidth;
	normalizedPosition_.y = spritePos.y / halfHeight;
}

void ReticleObject::UpdateRotation(float deltaTime) {
	if (!sprite_) return;

	// 回転角度を更新
	currentRotation_ += rotationSpeed_ * deltaTime;

	// 2πを超えたら0に戻す（オーバーフロー防止）
	const float TWO_PI = 3.14159f * 2.0f;
	if (currentRotation_ >= TWO_PI) {
		currentRotation_ -= TWO_PI;
	}

	// スプライトの回転を設定
	sprite_->GetSpriteTransform().rotate.z = currentRotation_;
}

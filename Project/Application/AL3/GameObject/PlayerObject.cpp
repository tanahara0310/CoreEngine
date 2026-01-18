#include "PlayerObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyboardInput.h"
#include "Engine/Input/GamepadInput.h"
#include "Engine/Math/MathCore.h"
#include "Engine/Collider/AABBCollider.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/GameObject/ReticleObject.h"
#include "Application/AL3/Camera/GameCameraController.h"
#include "Engine/WinApp/WinApp.h"
#include <dinput.h>
#include <numbers>

using namespace CoreEngine::MathCore;

void PlayerObject::Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture) {
	auto engine = GetEngineSystem();

	// モデルとテクスチャを外部から受け取る
	model_ = std::move(model);
	texture_ = texture;

	// Transformの初期化
	auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
	if (dxCommon) {
		transform_.Initialize(dxCommon->GetDevice());
	}

	// 初期位置・スケール設定
	transform_.translate = { 0.0f, 0.0f, 0.0f };
	transform_.scale = { 1.0f, 1.0f, 1.0f };
	transform_.rotate = { 0.0f, 0.0f, 0.0f };

	// アクティブ状態に設定
	SetActive(true);

	// アニメーション切り替えタイマーを初期化（デバッグ用自動切り替え - 将来的には削除予定）
	currentAnimationState_ = AnimationState::Idle;
	animationSwitchTimer_.SetName("AnimationSwitchTimer");

	// 弾発射のクールダウンタイマーを初期化
	shootCooldownTimer_.SetName("ShootCooldownTimer");

	// Gunアニメーションのタイマーを初期化
	gunAnimationTimer_.SetName("GunAnimationTimer");

	// ダメージ点滅タイマーを初期化
	damageFlashTimer_.SetName("DamageFlashTimer");
	isFlashing_ = false;
	flashElapsedTime_ = 0.0f;

	// 無敵時間タイマーを初期化
	invincibleTimer_.SetName("InvincibleTimer");
	isInvincible_ = false;

	// 死亡タイマーを初期化
	deathTimer_.SetName("DeathTimer");
	isDying_ = false;

	// コライダーを作成（サイズ: 幅1.5, 高さ2, 奥行1の矩形）
	collider_ = std::make_unique<CoreEngine::AABBCollider>(this, CoreEngine::Vector3{ 1.5f, 2.0f, 1.0f });
	collider_->SetLayer(CoreEngine::CollisionLayer::Player);
}

void PlayerObject::Update() {
	if (!IsActive() || !model_) {
		return;
	}

	auto engine = GetEngineSystem();

	// FrameRateControllerから1フレームあたりの時間を取得
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) {
		return;
	}

	float deltaTime = frameRateController->GetDeltaTime();

	// 死亡中は入力処理をスキップ
	if (!isDying_) {
		// 入力処理
		ProcessInput(deltaTime);
	}

	// Transformの更新
	transform_.TransferMatrix();

	// アニメーションの更新（コントローラー経由で自動的にスケルトンも更新される）
	if (model_->HasAnimationController()) {
		model_->UpdateAnimation(deltaTime);
	}

	// アニメーション切り替えタイマーの更新（デバッグ用 - 無効化中）
	// animationSwitchTimer_.Update(deltaTime);

	// 弾発射クールダウンタイマーの更新
	shootCooldownTimer_.Update(deltaTime);

	// Gunアニメーションタイマーの更新
	gunAnimationTimer_.Update(deltaTime);

	// ダメージ点滅タイマーの更新
	damageFlashTimer_.Update(deltaTime);

	// 無敵時間タイマーの更新
	invincibleTimer_.Update(deltaTime);

	// 死亡タイマーの更新
	deathTimer_.Update(deltaTime);

	// 点滅処理の更新
	if (isFlashing_) {
		flashElapsedTime_ += deltaTime;

		// 点滅終了判定
		if (flashElapsedTime_ >= damageFlashDuration_) {
			isFlashing_ = false;
			flashElapsedTime_ = 0.0f;

			// 通常色に戻す（マテリアルカラーをリセット）
			if (model_) {
				model_->SetMaterialColor(CoreEngine::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
			}
		} else {
			// 点滅エフェクト（0.1秒ごとに赤⇔白を切り替え）　
			float interval = fmodf(flashElapsedTime_, flashInterval_ * 2.0f);
			if (interval < flashInterval_) {
				// 赤色
				if (model_) {
					model_->SetMaterialColor(CoreEngine::Vector4{ 2.0f, 0.5f, 0.5f, 1.0f });
				}
			} else {
				// 白色
				if (model_) {
					model_->SetMaterialColor(CoreEngine::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
				}
			}
		}
	}
}

void PlayerObject::Draw(const CoreEngine::ICamera* camera) {
	if (!camera || !model_) return;

	// モデルの描画
	model_->Draw(transform_, camera, texture_.gpuHandle);
}

// ===== 移動処理 =====

void PlayerObject::ProcessInput(float deltaTime) {
	// 演出中は入力処理をスキップ
	if (isCutscenePlaying_) {
		return;
	}

	using CoreEngine::Vector2;
	using CoreEngine::Vector3;
	using CoreEngine::Vector4;
	using CoreEngine::Matrix4x4;

	auto engine = GetEngineSystem();
	auto inputManager = engine->GetComponent<CoreEngine::InputManager>();
	if (!inputManager) return;

	// 移動方向を計算
	Vector3 moveDirection = CalculateMoveDirection();

	// 現在の移動方向を保存
	currentMoveDirection_ = moveDirection;

	// 移動速度を計算
	float currentSpeed = 0.0f;
	if (moveDirection.x != 0.0f || moveDirection.z != 0.0f) {
		// 移動中は常に走る
		currentSpeed = runSpeed_;
		isMoving_ = true;
	} else {
		// 停止中
		isMoving_ = false;
		currentSpeed = 0.0f;
	}

	// キャラクターを移動
	MoveCharacter(moveDirection, deltaTime);

	// 弾を撃っているかチェック
	bool isShooting = ShouldShoot();

	// 向きを更新
	if (isShooting || gunAnimationTimer_.IsActive()) {
		// 弾を撃つ時、または射撃アニメーション中はレティクル方向を向く
		UpdateRotationToReticle();
	} else {
		// 通常時は移動方向に向きを更新
		UpdateRotation(moveDirection);
	}

	// 移動速度に応じてアニメーションを更新
	UpdateMovementAnimation(currentSpeed);
}

CoreEngine::Vector3 PlayerObject::CalculateMoveDirection() const {
	auto engine = GetEngineSystem();
	auto inputManager = engine->GetComponent<CoreEngine::InputManager>();
	if (!inputManager) return { 0.0f, 0.0f, 0.0f };

	// 入力値を取得（カメラ座標系ではまだない）
	CoreEngine::Vector3 inputDirection = { 0.0f, 0.0f, 0.0f };

	// キーボード入力
	auto keyboard = inputManager->GetKeyboard();
	if (keyboard) {
		if (keyboard->IsKeyPressed(DIK_W)) {
			inputDirection.z += 1.0f;  // 前
		}
		if (keyboard->IsKeyPressed(DIK_S)) {
			inputDirection.z -= 1.0f;  // 後
		}
		if (keyboard->IsKeyPressed(DIK_A)) {
			inputDirection.x -= 1.0f;  // 左
		}
		if (keyboard->IsKeyPressed(DIK_D)) {
			inputDirection.x += 1.0f;  // 右
		}
	}

	// ゲームパッド入力（左スティック）
	auto gamepad = inputManager->GetGamepad();
	if (gamepad && gamepad->IsConnected()) {
		CoreEngine::Stick leftStick = gamepad->GetLeftStick();

		// スティック入力を加算（キーボード入力と合算）　
		inputDirection.x += leftStick.x;
		inputDirection.z += leftStick.y;  // Y軸を前後方向に
	}

	// 入力がない場合は早期リターン
	if (inputDirection.x == 0.0f && inputDirection.z == 0.0f) {
		return { 0.0f, 0.0f, 0.0f };
	}

	// カメラコントローラーがある場合はカメラ相対で移動
	if (cameraController_) {
		CoreEngine::Vector3 cameraForward = cameraController_->GetCameraForward();
		CoreEngine::Vector3 cameraRight = cameraController_->GetCameraRight();

		// カメラの前方向と右方向を使ってワールド座標系の移動方向を計算
		CoreEngine::Vector3 direction = { 0.0f, 0.0f, 0.0f };

		// 前後方向（inputDirection.z）
		direction = Vector::Add(direction, Vector::Multiply(inputDirection.z, cameraForward));

		// 左右方向（inputDirection.x）
		direction = Vector::Add(direction, Vector::Multiply(inputDirection.x, cameraRight));

		// Y成分をゼロにする（水平移動のみ）
		direction.y = 0.0f;

		// 正規化
		float length = Vector::Length(direction);
		if (length > 0.0f) {
			direction = Vector::Multiply(1.0f / length, direction);
		}

		return direction;
	}

	// カメラコントローラーがない場合はワールド座標系で移動（フォールバック）
	float length = Vector::Length(inputDirection);
	if (length > 0.0f) {
		inputDirection = Vector::Multiply(1.0f / length, inputDirection);
	}

	return inputDirection;
}

void PlayerObject::MoveCharacter(const CoreEngine::Vector3& direction, float deltaTime) {
	if (!isMoving_) return;

	// 常に走る速度で移動
	float speed = runSpeed_;

	// 移動量を計算（MathCoreを使用）
	CoreEngine::Vector3 velocity = Vector::Multiply(speed * deltaTime, direction);

	// 座標を更新
	transform_.translate = Vector::Add(transform_.translate, velocity);

	// 壁の移動制限を適用
	// アリーナサイズ（壁の中心までの距離）
	constexpr float ARENA_SIZE = 40.0f;  // 30 → 40に拡大
	// 壁モデルのサイズ（スケール1.0の場合のモデルの実サイズ）
	constexpr float WALL_MODEL_SIZE = 23.0f;
	// プレイヤーの移動可能範囲（壁の中心から壁モデルサイズの半分手前）
	constexpr float MOVE_LIMIT = ARENA_SIZE - (WALL_MODEL_SIZE * 0.5f);

	// X座標とZ座標をクランプ
	transform_.translate.x = std::clamp(transform_.translate.x, -MOVE_LIMIT, MOVE_LIMIT);
	transform_.translate.z = std::clamp(transform_.translate.z, -MOVE_LIMIT, MOVE_LIMIT);
}

void PlayerObject::UpdateMovementAnimation(float moveSpeed) {
	if (!model_) return;

	// 死亡中はアニメーションを切り替えない
	if (isDying_) {
		return;
	}

	// Gunアニメーション再生中は移動アニメーションに切り替えない
	if (gunAnimationTimer_.IsActive()) {
		return;
	}

	AnimationState targetState = AnimationState::Idle;

	// 移動中は走るアニメーション、停止中は待機アニメーション
	if (moveSpeed > 0.0f) {
		targetState = AnimationState::Run;
	} else {
		targetState = AnimationState::Idle;
	}

	// アニメーション状態が変わった場合のみ切り替え
	if (targetState != currentAnimationState_) {
		currentAnimationState_ = targetState;

		switch (currentAnimationState_) {
		case AnimationState::Idle:
			model_->SwitchAnimationWithBlend("playerIdleAnimation", 0.3f, true);
			break;
		case AnimationState::Walk:
			model_->SwitchAnimationWithBlend("playerWalkAnimation", 0.3f, true);
			break;
		case AnimationState::Run:
			model_->SwitchAnimationWithBlend("playerRunAnimation", 0.3f, true);
			break;
		case AnimationState::Gun:
			model_->SwitchAnimationWithBlend("playerGunAnimation", 0.3f, true);
			break;
		case AnimationState::Die:
			model_->SwitchAnimationWithBlend("playerDieAnimation", 0.3f, false);
			break;
		}
	}
}

void PlayerObject::UpdateRotation(const CoreEngine::Vector3& direction) {
if (direction.x == 0.0f && direction.z == 0.0f) return;

	constexpr float PI = std::numbers::pi_v<float>;

	// 移動方向の角度を計算（Y軸周りの回転）
	float targetAngle = std::atan2f(direction.x, direction.z);

	// モデルの初期向きが-Z方向の場合、180度回転させる
	targetAngle += PI;

	// 現在の角度
	float currentAngle = transform_.rotate.y;

	// 角度差を計算（-π～πの範囲に正規化）
	float angleDiff = targetAngle - currentAngle;

	// -πからπの範囲に正規化
	while (angleDiff > PI) {
		angleDiff -= 2.0f * PI;
	}
	while (angleDiff < -PI) {
		angleDiff += 2.0f * PI;
	}

	// 補間して滑らかに回転（移動時は速めに回転）
	auto engine = GetEngineSystem();
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) return;

	float deltaTime = frameRateController->GetDeltaTime();
	float targetRotationSpeed = rotationSpeed_ * 1.5f;
	float rotationAmount = angleDiff * targetRotationSpeed * deltaTime;

	// 回転を適用
	transform_.rotate.y += rotationAmount;
}

// レティクル方向（弾の発射方向）に向きを更新する処理
void PlayerObject::UpdateRotationToReticle() {
	// レティクルとカメラが設定されていない場合は何もしない
	if (!reticle_ || !camera_) return;

	using CoreEngine::Vector2;
	using CoreEngine::Vector3;
	using CoreEngine::Vector4;
	using CoreEngine::Matrix4x4;
	using namespace CoreEngine::MathCore;

	constexpr float PI = std::numbers::pi_v<float>;

	// レティクルの正規化座標を取得（-1.0～1.0の範囲）
	Vector2 reticleNormalizedPos = reticle_->GetNormalizedPosition();

	// 画面サイズを取得
	float screenWidth = static_cast<float>(CoreEngine::WinApp::kClientWidth);
	float screenHeight = static_cast<float>(CoreEngine::WinApp::kClientHeight);

	// ビュー・プロジェクション行列を取得
	Matrix4x4 viewMatrix = camera_->GetViewMatrix();
	Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();

	// プレイヤーの位置
	Vector3 playerPos = transform_.translate;

	// レティクルの正規化座標からスクリーン座標を計算
	float screenX = (reticleNormalizedPos.x + 1.0f) * 0.5f * screenWidth;
	float screenY = (-reticleNormalizedPos.y + 1.0f) * 0.5f * screenHeight;

	// スクリーン座標から正規化デバイス座標(NDC)に変換
	float ndcX = (screenX / screenWidth) * 2.0f - 1.0f;
	float ndcY = 1.0f - (screenY / screenHeight) * 2.0f;

	// near平面とfar平面でのNDC座標
	Vector3 nearNDC = { ndcX, ndcY, 0.0f };
	Vector3 farNDC = { ndcX, ndcY, 1.0f };

	// ビュー・プロジェクション行列の逆行列を計算
	Matrix4x4 viewProjection = Matrix::Multiply(viewMatrix, projectionMatrix);
	Matrix4x4 invViewProjection = Matrix::Inverse(viewProjection);

	// NDC座標をワールド座標に変換
	Vector4 nearWorld4 = CoordinateTransform::TransformCoord(
		Vector4{ nearNDC.x, nearNDC.y, nearNDC.z, 1.0f },
		invViewProjection
	);
	Vector4 farWorld4 = CoordinateTransform::TransformCoord(
		Vector4{ farNDC.x, farNDC.y, farNDC.z, 1.0f },
		invViewProjection
	);

	// 同次座標からワールド座標に変換（w除算）
	Vector3 nearWorld = {
		nearWorld4.x / nearWorld4.w,
		nearWorld4.y / nearWorld4.w,
		nearWorld4.z / nearWorld4.w
	};
	Vector3 farWorld = {
		farWorld4.x / farWorld4.w,
		farWorld4.y / farWorld4.w,
		farWorld4.z / farWorld4.w
	};

	// カメラからレティクルへのレイ方向を計算
	Vector3 rayDirection = Vector::Subtract(farWorld, nearWorld);
	rayDirection = Vector::Normalize(rayDirection);

	// プレイヤーのY座標でレイと交差する点を計算
	float t = 0.0f;
	if (std::abs(rayDirection.y) > 0.0001f) {
		t = (playerPos.y - nearWorld.y) / rayDirection.y;
	} else {
		t = 100.0f;
	}

	// レイ上の交差点を計算
	Vector3 targetWorldPos = {
		nearWorld.x + t * rayDirection.x,
		playerPos.y,
		nearWorld.z + t * rayDirection.z
	};

	// プレイヤーからターゲット位置への方向ベクトルを計算
	Vector3 direction = Vector::Subtract(targetWorldPos, playerPos);
	direction.y = 0.0f;

	// 正規化
	float length = Vector::Length(direction);
	if (length < 0.0001f) {
		// 方向が計算できない場合は現在の向きを維持
		return;
	}

	direction = Vector::Multiply(1.0f / length, direction);

	// 方向から角度を計算（Y軸周りの回転）
	float targetAngle = std::atan2f(direction.x, direction.z);

	// モデルの初期向きが-Z方向の場合、180度回転させる
	targetAngle += PI;

	// 現在の角度
	float currentAngle = transform_.rotate.y;

	// 角度差を計算（-π～πの範囲に正規化）
	float angleDiff = targetAngle - currentAngle;

	// -πからπの範囲に正規化
	while (angleDiff > PI) {
		angleDiff -= 2.0f * PI;
	}
	while (angleDiff < -PI) {
		angleDiff += 2.0f * PI;
	}

	// 補間して滑らかに回転（射撃時は高速回転）
	auto engine = GetEngineSystem();
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) return;

	float deltaTime = frameRateController->GetDeltaTime();
	float targetRotationSpeed = rotationSpeed_ * 10.0f;  // 射撃時は通常の10倍速で回転（即座に向く）
	float rotationAmount = angleDiff * targetRotationSpeed * deltaTime;

	// 回転を適用
	transform_.rotate.y += rotationAmount;
}

// ===== アニメーション管理 =====

void PlayerObject::OnAnimationSwitch() {
	if (!model_) return;

	// 次のアニメーション状態を取得
	currentAnimationState_ = GetNextAnimationState();

	// アニメーションをブレンドしながら切り替え（0.5秒かけて滑らかに）
	switch (currentAnimationState_) {
	case AnimationState::Idle:
		model_->SwitchAnimationWithBlend("playerIdleAnimation", 0.5f, true);
		break;
	case AnimationState::Walk:
		model_->SwitchAnimationWithBlend("playerWalkAnimation", 0.5f, true);
		break;
	case AnimationState::Run:
		model_->SwitchAnimationWithBlend("playerRunAnimation", 0.5f, true);
		break;
	case AnimationState::Gun:
		model_->SwitchAnimationWithBlend("playerGunAnimation", 0.5f, true);
		break;
	}
}

PlayerObject::AnimationState PlayerObject::GetNextAnimationState() const {
	// 待機 -> 歩く -> 走る -> 銃を構える -> 待機 のループ
	switch (currentAnimationState_) {
	case AnimationState::Idle:
		return AnimationState::Walk;
	case AnimationState::Walk:
		return AnimationState::Run;
	case AnimationState::Run:
		return AnimationState::Gun;
	case AnimationState::Gun:
		return AnimationState::Idle;
	default:
		return AnimationState::Idle;
	}
}

// ===== 弾発射関連 =====

bool PlayerObject::ShouldShoot() const {
auto engine = GetEngineSystem();
auto inputManager = engine->GetComponent<CoreEngine::InputManager>();
if (!inputManager) return false;

	// クールダウン中は発射できない
	if (shootCooldownTimer_.IsActive()) {
		return false;
	}

	// キーボード：スペースキーが押されているかチェック
	auto keyboard = inputManager->GetKeyboard();
	if (keyboard && keyboard->IsKeyPressed(DIK_SPACE)) {
		return true;
	}

	// ゲームパッド：RTトリガーが押されているかチェック（閾値0.5以上）
	auto gamepad = inputManager->GetGamepad();
	if (gamepad && gamepad->IsConnected()) {
		constexpr float TRIGGER_THRESHOLD = 0.5f;
		if (gamepad->GetRightTrigger() >= TRIGGER_THRESHOLD) {
			return true;
		}
	}

	return false;
}

void PlayerObject::StartShootCooldown() {
	shootCooldownTimer_.Start(shootCooldown_, false);
}

void PlayerObject::PlayShootAnimation() {
	if (!model_) return;

	// 弾発射SEを再生
	if (bulletShotSE_ && bulletShotSE_->IsValid()) {
		bulletShotSE_->Play(false);
	}

	// 既にGunアニメーション中の場合は、タイマーを延長するだけ
	if (gunAnimationTimer_.IsActive()) {
		// タイマーをリセットして延長
		gunAnimationTimer_.Stop();
		gunAnimationTimer_.Start(gunAnimationDuration_, false);

		// コールバックを再設定
		gunAnimationTimer_.SetOnComplete([this]() {
			if (isMoving_) {
				currentAnimationState_ = AnimationState::Run;
				model_->SwitchAnimationWithBlend("playerRunAnimation", 0.3f, true);
			} else {
				currentAnimationState_ = AnimationState::Idle;
				model_->SwitchAnimationWithBlend("playerIdleAnimation", 0.3f, true);
			}
			});
		return;
	}

	// 新規にGunアニメーションを開始
	currentAnimationState_ = AnimationState::Gun;
	model_->SwitchAnimationWithBlend("playerGunAnimation", 0.1f, true);

	// Gunアニメーションタイマーを開始
	gunAnimationTimer_.Start(gunAnimationDuration_, false);

	// タイマー終了時に移動状態に応じたアニメーションに戻す
	gunAnimationTimer_.SetOnComplete([this]() {
		if (isMoving_) {
			currentAnimationState_ = AnimationState::Run;
			model_->SwitchAnimationWithBlend("playerRunAnimation", 0.3f, true);
		} else {
			currentAnimationState_ = AnimationState::Idle;
			model_->SwitchAnimationWithBlend("playerIdleAnimation", 0.3f, true);
		}
		});
}

CoreEngine::Vector3 PlayerObject::GetBulletSpawnPosition() const {
	return {
		transform_.translate.x,
		transform_.translate.y,
		transform_.translate.z
	};
}

CoreEngine::Vector3 PlayerObject::GetBulletDirection() const {
using CoreEngine::Vector2;
using CoreEngine::Vector3;
using CoreEngine::Vector4;
using CoreEngine::Matrix4x4;
using namespace CoreEngine::MathCore;

// レティクルが設定されている場合、レティクル方向を計算
if (reticle_ && camera_) {
// レティクルの正規化座標を取得（-1.0～1.0の範囲）
Vector2 reticleNormalizedPos = reticle_->GetNormalizedPosition();

#ifdef _DEBUG
		// デバッグ出力
		OutputDebugStringA(("Reticle Normalized Pos: x=" + std::to_string(reticleNormalizedPos.x) +
			", y=" + std::to_string(reticleNormalizedPos.y) + "\n").c_str());
#endif

		// 画面サイズを取得
		float screenWidth = static_cast<float>(CoreEngine::WinApp::kClientWidth);
		float screenHeight = static_cast<float>(CoreEngine::WinApp::kClientHeight);

		// ビュー・プロジェクション行列を取得
		Matrix4x4 viewMatrix = camera_->GetViewMatrix();
		Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();

		// プレイヤーの位置
		Vector3 playerPos = transform_.translate;

		// ===== 修正：レティクルのスクリーン位置を使ってカメラからのレイを計算 =====

		// レティクルの正規化座標からスクリーン座標を計算
		// 正規化座標(-1?1)をスクリーン座標(0?width/height)に変換
		float screenX = (reticleNormalizedPos.x + 1.0f) * 0.5f * screenWidth;
		float screenY = (-reticleNormalizedPos.y + 1.0f) * 0.5f * screenHeight;  // Y軸は反転（スクリーン座標系）

#ifdef _DEBUG
		OutputDebugStringA(("Screen Pos: x=" + std::to_string(screenX) +
			", y=" + std::to_string(screenY) + "\n").c_str());
#endif

		// スクリーン座標から正規化デバイス座標(NDC)に変換
		float ndcX = (screenX / screenWidth) * 2.0f - 1.0f;
		float ndcY = 1.0f - (screenY / screenHeight) * 2.0f;  // Y軸は上が+1

		// near平面とfar平面でのNDC座標（同じXY、異なるZ）
		Vector3 nearNDC = { ndcX, ndcY, 0.0f };   // near平面
		Vector3 farNDC = { ndcX, ndcY, 1.0f };    // far平面

		// ビュー・プロジェクション行列の逆行列を計算
		Matrix4x4 viewProjection = Matrix::Multiply(viewMatrix, projectionMatrix);
		Matrix4x4 invViewProjection = Matrix::Inverse(viewProjection);

		// NDC座標をワールド座標に変換（同次座標を使用）
		Vector4 nearWorld4 = CoordinateTransform::TransformCoord(
			Vector4{ nearNDC.x, nearNDC.y, nearNDC.z, 1.0f },
			invViewProjection
		);
		Vector4 farWorld4 = CoordinateTransform::TransformCoord(
			Vector4{ farNDC.x, farNDC.y, farNDC.z, 1.0f },
			invViewProjection
		);

		// 同次座標からワールド座標に変換（w除算）
		Vector3 nearWorld = {
			nearWorld4.x / nearWorld4.w,
			nearWorld4.y / nearWorld4.w,
			nearWorld4.z / nearWorld4.w
		};
		Vector3 farWorld = {
			farWorld4.x / farWorld4.w,
			farWorld4.y / farWorld4.w,
			farWorld4.z / farWorld4.w
		};

#ifdef _DEBUG
		OutputDebugStringA(("Near World: x=" + std::to_string(nearWorld.x) +
			", y=" + std::to_string(nearWorld.y) +
			", z=" + std::to_string(nearWorld.z) + "\n").c_str());
		OutputDebugStringA(("Far World: x=" + std::to_string(farWorld.x) +
			", y=" + std::to_string(farWorld.y) +
			", z=" + std::to_string(farWorld.z) + "\n").c_str());
#endif

		// カメラからレティクルへのレイ方向を計算
		Vector3 rayDirection = Vector::Subtract(farWorld, nearWorld);
		rayDirection = Vector::Normalize(rayDirection);

#ifdef _DEBUG
		OutputDebugStringA(("Player Pos: x=" + std::to_string(playerPos.x) +
			", y=" + std::to_string(playerPos.y) +
			", z=" + std::to_string(playerPos.z) + "\n").c_str());
		OutputDebugStringA(("Ray Direction: x=" + std::to_string(rayDirection.x) +
			", y=" + std::to_string(rayDirection.y) +
			", z=" + std::to_string(rayDirection.z) + "\n").c_str());
#endif

		// プレイヤーのY座標でレイと交差する点を計算
		// レイの方程式: P = nearWorld + t * rayDirection
		// Y座標が一致する点: playerPos.y = nearWorld.y + t * rayDirection.y
		float t = 0.0f;
		if (std::abs(rayDirection.y) > 0.0001f) {
			t = (playerPos.y - nearWorld.y) / rayDirection.y;
		} else {
			// Y方向の変化がほぼない場合は、十分遠くの点を使用
			t = 100.0f;
		}

#ifdef _DEBUG
		OutputDebugStringA(("t value: " + std::to_string(t) + "\n").c_str());
#endif

		// レイ上の交差点を計算
		Vector3 targetWorldPos = {
			nearWorld.x + t * rayDirection.x,
			playerPos.y,  // プレイヤーと同じY座標
			nearWorld.z + t * rayDirection.z
		};

#ifdef _DEBUG
		OutputDebugStringA(("Target World Pos: x=" + std::to_string(targetWorldPos.x) +
			", y=" + std::to_string(targetWorldPos.y) +
			", z=" + std::to_string(targetWorldPos.z) + "\n").c_str());
#endif

		// プレイヤーからターゲット位置への方向ベクトルを計算
		Vector3 direction = Vector::Subtract(targetWorldPos, playerPos);

		// XZ平面に射影（Y成分を0にする）
		direction.y = 0.0f;

		// 正規化
		float length = Vector::Length(direction);
		if (length > 0.0f) {
			direction = Vector::Multiply(1.0f / length, direction);

#ifdef _DEBUG
			OutputDebugStringA(("Final Direction: x=" + std::to_string(direction.x) +
				", y=" + std::to_string(direction.y) +
				", z=" + std::to_string(direction.z) + "\n").c_str());
			OutputDebugStringA("---\n");
#endif

			return direction;
		}
	}

	// レティクルが設定されていない場合はプレイヤーの前方向き（Y軸回転に基づく)
	// プレイヤーモデルは180度回転しているため、弾の方向も逆にする
	float angle = transform_.rotate.y;
	return {
		-std::sinf(angle),
		0.0f,
		-std::cosf(angle)
	};
}

void PlayerObject::OnCollisionEnter(GameObject* other) {
	if (!other) return;

	// 無敵中はダメージを受けない
	if (isInvincible_) {
		return;
	}

	// ボスと衝突した場合の処理
	if (dynamic_cast<BossObject*>(other)) {
		// ダメージを受ける
		TakeDamage(5);  // 5ダメージ

	}
}

void PlayerObject::StartDamageFlash() {
	// 点滅を開始
	isFlashing_ = true;
	flashElapsedTime_ = 0.0f;

	// タイマーを開始
	damageFlashTimer_.Start(damageFlashDuration_, false);

	// タイマー終了時に通常色に戻す
	damageFlashTimer_.SetOnComplete([this]() {
		isFlashing_ = false;
		flashElapsedTime_ = 0.0f;
		if (model_) {
			model_->SetMaterialColor(CoreEngine::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
		}
		});
}

void PlayerObject::TakeDamage(int damage) {
	// 死亡中または無敵中はダメージを受けない
	if (isDying_ || isInvincible_) {
		return;
	}

	// ダメージを受ける
	currentHP_ -= damage;

	// HPが0未満にならないようにクランプ
	if (currentHP_ < 0) {
		currentHP_ = 0;
	}

	// HPが0になった場合、死亡アニメーションを再生
	if (currentHP_ <= 0) {
		PlayDeathAnimation();
		return;
	}

	// ダメージSEを再生
	if (playerHitSE_ && playerHitSE_->IsValid()) {
		playerHitSE_->Play(false);
	}

	// ダメージ点滅を開始
	StartDamageFlash();

	// 無敵時間を開始
	isInvincible_ = true;
	invincibleTimer_.Start(invincibleDuration_, false);
	invincibleTimer_.SetOnComplete([this]() {
		isInvincible_ = false;
		});
}

void PlayerObject::PlayDeathAnimation() {
	if (!model_ || isDying_) return;

	// 死亡状態に設定
	isDying_ = true;
	currentAnimationState_ = AnimationState::Die;

	// 死亡SEを再生
	if (playerDieSE_ && playerDieSE_->IsValid()) {
		playerDieSE_->Play(false);
	}

	// 死亡アニメーションに切り替え（ループなし）
	model_->SwitchAnimationWithBlend("playerDieAnimation", 0.3f, false);

	// 死亡タイマーを開始
	deathTimer_.Start(deathDuration_, false);

	// タイマー終了時の処理（必要に応じてゲームオーバー処理を呼び出すなど）
	deathTimer_.SetOnComplete([this]() {
		// TODO: ゲームオーバー処理やシーン遷移などを実装
		// 現時点では何もしない
		});
}

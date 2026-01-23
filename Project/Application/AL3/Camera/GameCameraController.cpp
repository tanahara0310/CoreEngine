#include "GameCameraController.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Camera/Release/Camera.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Graphics/PostEffect/PostEffectManager.h"
#include "Engine/Graphics/PostEffect/Effect/FadeEffect.h"
#include "Engine/Graphics/PostEffect/Effect/RadialBlur.h"
#include "Engine/Graphics/PostEffect/PostEffectNames.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "Application/AL3/GameObject/BossObject.h"
#include <numbers>

using namespace CoreEngine;
using namespace CoreEngine::MathCore;

void GameCameraController::Initialize(CoreEngine::EngineSystem* engine, CoreEngine::ICamera* camera, PlayerObject* player, BossObject* boss) {
	engine_ = engine;
	camera_ = camera;
	player_ = player;
	boss_ = boss;

	// タイマーの初期化
	bossIntroTimer_.SetName("BossIntroTimer");
	bossFocusTimer_.SetName("BossFocusTimer");
	moveToPlayerTimer_.SetName("MoveToPlayerTimer");
	fadeOutTimer_.SetName("FadeOutTimer");
	fadeInTimer_.SetName("FadeInTimer");
	bossDefeatTimer_.SetName("BossDefeatTimer");
	playerDeathTimer_.SetName("PlayerDeathTimer");

	// 初期状態はボス登場イントロ演出
	currentState_ = CameraState::OpeningBossIntro;
}

void GameCameraController::Update() {
	if (!camera_ || !player_ || !boss_) return;

	auto frameRateController = engine_->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) return;

	float deltaTime = frameRateController->GetDeltaTime();

	// タイマーの更新
	bossIntroTimer_.Update(deltaTime);
	bossFocusTimer_.Update(deltaTime);
	moveToPlayerTimer_.Update(deltaTime);
	fadeOutTimer_.Update(deltaTime);
	fadeInTimer_.Update(deltaTime);
	bossDefeatTimer_.Update(deltaTime);
	playerDeathTimer_.Update(deltaTime);

	// 状態に応じた更新処理
	switch (currentState_) {
	case CameraState::OpeningBossIntro:
		UpdateOpeningBossIntro();
		break;
	case CameraState::OpeningBossFocus:
		UpdateOpeningBossFocus();
		break;
	case CameraState::OpeningToPlayer:
		UpdateOpeningToPlayer();
		break;
	case CameraState::OpeningFadeOut:
		UpdateOpeningFadeOut();
		break;
	case CameraState::OpeningFadeIn:
		UpdateOpeningFadeIn();
		break;
	case CameraState::FollowPlayer:
		UpdateFollowPlayer();
		break;
	case CameraState::BossDefeatFocus:
		UpdateBossDefeatFocus();
		break;
	case CameraState::PlayerDeathFocus:
		UpdatePlayerDeathFocus();
		break;
	}
}

void GameCameraController::StartOpeningSequence() {
	// ボス登場演出を開始
	currentState_ = CameraState::OpeningBossIntro;

	// ボス登場イントロタイマーを開始
	bossIntroTimer_.Start(bossIntroDuration_, false);

	// タイマー終了時に次の状態へ遷移
	bossIntroTimer_.SetOnComplete([this]() {
		// ボス登場演出に移行
		currentState_ = CameraState::OpeningBossFocus;

		// ボス登場演出タイマーを開始
		bossFocusTimer_.Start(bossFocusDuration_, false);

		// タイマー終了時に次の状態へ遷移
		bossFocusTimer_.SetOnComplete([this]() {
			// プレイヤーへの移動演出に移行
			currentState_ = CameraState::OpeningToPlayer;

			// 現在のカメラ位置と注視点を保存
			transitionStartPosition_ = camera_->GetPosition();
			Vector3 bossPosition = boss_->GetTransform().translate;
			transitionStartTarget_ = Vector::Add(bossPosition, bossOpeningCameraTargetOffset_);

			// プレイヤーへの移動タイマーを開始
			moveToPlayerTimer_.Start(moveToPlayerDuration_, false);

			// 移動終了時にフェードアウトに切り替え
			moveToPlayerTimer_.SetOnComplete([this]() {
				// フェードアウト開始
				currentState_ = CameraState::OpeningFadeOut;
				
				// フェードエフェクトを取得して有効化
				auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
				if (postEffectManager) {
					auto* fadeEffect = postEffectManager->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);
					if (fadeEffect) {
						fadeEffect->SetEnabled(true);  // エフェクトを有効化
						fadeEffect->SetFadeType(FadeEffect::FadeType::BlackFade);
						fadeEffect->SetFadeAlpha(0.0f);  // 初期値は透明
					}
				}
				
				fadeOutTimer_.Start(fadeDuration_, false);
				
				fadeOutTimer_.SetOnComplete([this]() {
					// フェードアウト完了後、カメラをプレイヤー追従位置に即座に移動
					currentState_ = CameraState::OpeningFadeIn;
					
					// カメラをプレイヤー追従位置に設定（ワープ）
					Vector3 playerPosition = player_->GetTransform().translate;
					Vector3 bossPosition = boss_->GetTransform().translate;
					Vector3 playerToBoss = Vector::Subtract(bossPosition, playerPosition);
					Vector3 playerToBossDir = Vector::Normalize(playerToBoss);
					Vector3 cameraDirection = Vector::Multiply(-1.0f, playerToBossDir);
					
					float cameraDistance = Vector::Length(Vector3{ playerCameraOffset_.x, 0.0f, playerCameraOffset_.z });
					float cameraHeight = playerCameraOffset_.y;
					
					Vector3 horizontalOffset = Vector::Multiply(cameraDistance, cameraDirection);
					horizontalOffset.y = 0.0f;
					
					Vector3 newCameraPosition = playerPosition;
					newCameraPosition = Vector::Add(newCameraPosition, horizontalOffset);
					newCameraPosition.y += cameraHeight;
					
					Vector3 midPoint = Vector::Add(playerPosition, Vector::Multiply(0.5f, playerToBoss));
					Vector3 targetLookAt = Vector::Add(midPoint, Vector3{ 0.0f, 1.0f, 0.0f });
					
					SetCameraPositionAndTarget(newCameraPosition, targetLookAt);
					
					// フェードイン開始
					fadeInTimer_.Start(fadeDuration_, false);
					
					fadeInTimer_.SetOnComplete([this]() {
						// フェードイン完了後、プレイヤー追従に切り替え
						currentState_ = CameraState::FollowPlayer;
						
						// フェードエフェクトを無効化
						auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
						if (postEffectManager) {
							auto* fadeEffect = postEffectManager->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);
							if (fadeEffect) {
								fadeEffect->SetFadeAlpha(0.0f);
								fadeEffect->SetEnabled(false);  // エフェクトを無効化
							}
						}
						
						// 演出終了コールバックを呼び出し
						if (onOpeningFinishedCallback_) {
							onOpeningFinishedCallback_();
						}
					});
				});
			});
		});
	});
}

void GameCameraController::UpdateOpeningBossIntro() {
	// ボス登場イントロ演出 - ラジアルブラー付きで下から見上げる
	Vector3 bossPosition = boss_->GetTransform().translate;
	
	// 演出の進行度を取得（0.0 → 1.0）
	float t = bossIntroTimer_.GetElapsedTime() / bossIntroDuration_;
	
	// カメラを下から見上げる位置に配置
	Vector3 cameraOffset = { 0.0f, -5.0f, -20.0f };  // 低い位置から遠くに配置
	Vector3 cameraPosition = Vector::Add(bossPosition, cameraOffset);
	
	// ボスの全身を注視（少し上を見る）
	Vector3 targetPosition = Vector::Add(bossPosition, Vector3{ 0.0f, 4.0f, 0.0f });
	
	SetCameraPositionAndTarget(cameraPosition, targetPosition);
	
	// ラジアルブラーエフェクトを適用
	auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
	if (postEffectManager) {
		auto* radialBlur = postEffectManager->GetEffect<RadialBlur>(PostEffectNames::RadialBlur);
		if (radialBlur) {
			// イントロ演出開始時にラジアルブラーを有効化
			if (!radialBlur->IsEnabled()) {
				radialBlur->SetEnabled(true);
			}
			
			// ブラーの強度を演出の進行に合わせて変化
			RadialBlur::RadialBlurParams params;
			params.intensity = 1.5f * (1.0f - t);  // 徐々に弱くなる（1.5 → 0.0）
			params.sampleCount = 12.0f;
			params.centerX = 0.5f;
			params.centerY = 0.6f;  // 少し上に中心を配置（ボスの位置）
			radialBlur->SetParams(params);
		}
	}
}

void GameCameraController::UpdateOpeningBossFocus() {
	// ボスを様々な角度から見渡す演出
	Vector3 bossPosition = boss_->GetTransform().translate;
	
	// 演出の進行度を取得（0.0 → 1.0）
	float t = bossFocusTimer_.GetElapsedTime() / bossFocusDuration_;
	
	// カメラをボスの周りを一周回るように配置
	float angle = std::numbers::pi_v<float> * 2.0f * t;  // 0° → 360°回転（一周）
	float radius = 18.0f;          // ボスからの距離
	float height = 10.0f;          // カメラの高さ
	
	// 円周上のカメラ位置を計算
	Vector3 cameraOffset = {
		std::sinf(angle) * radius,
		height - t * 3.0f,  // 徐々に下がる（10 → 7）
		-std::cosf(angle) * radius
	};
	
	Vector3 cameraPosition = Vector::Add(bossPosition, cameraOffset);
	
	// ボスの上半身を注視
	float twoPi = std::numbers::pi_v<float> * 2.0f;
	Vector3 targetOffset = { 0.0f, 3.0f + std::sinf(t * twoPi) * 0.5f, 0.0f };  // 微妙に上下に揺れる
	Vector3 targetPosition = Vector::Add(bossPosition, targetOffset);
	
	SetCameraPositionAndTarget(cameraPosition, targetPosition);
	
	// ラジアルブラーを徐々にフェードアウト
	auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
	if (postEffectManager) {
		auto* radialBlur = postEffectManager->GetEffect<RadialBlur>(PostEffectNames::RadialBlur);
		if (radialBlur && radialBlur->IsEnabled()) {
			// ブラーの強度を演出の進行に合わせて減少
			RadialBlur::RadialBlurParams params;
			params.intensity = 0.3f * (1.0f - t);  // 徐々に弱くなる（0.3 → 0.0）
			params.sampleCount = 10.0f;
			params.centerX = 0.5f;
			params.centerY = 0.5f;
			radialBlur->SetParams(params);
			
			// 演出終了時にラジアルブラーを無効化
			if (t >= 0.95f) {
				radialBlur->SetEnabled(false);
			}
		}
	}
}

void GameCameraController::UpdateOpeningToPlayer() {
	// プレイヤーへの移動をイージング補間
	float t = moveToPlayerTimer_.GetElapsedTime() / moveToPlayerDuration_;

	// 目標位置と注視点を計算
	Vector3 playerPosition = player_->GetTransform().translate;
	Vector3 targetCameraPosition = Vector::Add(playerPosition, playerCameraOffset_);
	Vector3 targetLookAt = Vector::Add(playerPosition, Vector3{ 0.0f, 1.0f, 0.0f });

	// イージングを適用して補間
	Vector3 currentPosition = EasingUtil::LerpVector3(
		transitionStartPosition_,
		targetCameraPosition,
		t,
		easingType_
	);

	Vector3 currentTarget = EasingUtil::LerpVector3(
		transitionStartTarget_,
		targetLookAt,
		t,
		easingType_
	);

	SetCameraPositionAndTarget(currentPosition, currentTarget);
}

void GameCameraController::UpdateFollowPlayer() {
	// プレイヤーを追従しながらボスも視界に入れ続ける
	auto frameRateController = engine_->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) return;

	float deltaTime = frameRateController->GetDeltaTime();

	// プレイヤーとボスの位置を取得
	Vector3 playerPosition = player_->GetTransform().translate;
	Vector3 bossPosition = boss_->GetTransform().translate;

	// プレイヤーからボスへの方向ベクトル
	Vector3 playerToBoss = Vector::Subtract(bossPosition, playerPosition);
	Vector3 playerToBossDir = Vector::Normalize(playerToBoss);

	// カメラをプレイヤーの後ろ側に配置（ボスの反対方向）
	Vector3 cameraDirection = Vector::Multiply(-1.0f, playerToBossDir);

	// カメラの高さと距離を取得
	float cameraDistance = Vector::Length(Vector3{ playerCameraOffset_.x, 0.0f, playerCameraOffset_.z });
	float cameraHeight = playerCameraOffset_.y;

	// カメラの水平方向のオフセットを計算（ボスの反対方向に配置）
	Vector3 horizontalOffset = Vector::Multiply(cameraDistance, cameraDirection);
	horizontalOffset.y = 0.0f; // 水平方向のみ

	// カメラの最終位置を計算（プレイヤーの後ろ + 高さ）
	Vector3 targetCameraPosition = playerPosition;
	targetCameraPosition = Vector::Add(targetCameraPosition, horizontalOffset);
	targetCameraPosition.y += cameraHeight;

	// プレイヤーとボスの中間地点を計算（注視点）
	Vector3 midPoint = Vector::Add(playerPosition, Vector::Multiply(0.5f, playerToBoss));

	// 注視点の高さを調整（プレイヤーとボスの中心あたり）
	Vector3 targetLookAt = Vector::Add(midPoint, Vector3{ 0.0f, 1.0f, 0.0f });

	// 現在のカメラ位置から目標位置へ滑らかに移動
	Vector3 currentPosition = camera_->GetPosition();
	Vector3 newPosition = EasingUtil::LerpVector3(
		currentPosition,
		targetCameraPosition,
		cameraFollowSpeed_ * deltaTime,
		EasingUtil::Type::Linear
	);

	SetCameraPositionAndTarget(newPosition, targetLookAt);

	// カメラの方向ベクトルをキャッシュ（入力処理で使用）
	Vector3 cameraToTarget = Vector::Subtract(targetLookAt, newPosition);
	cameraToTarget.y = 0.0f; // XZ平面に投影
	currentCameraForward_ = Vector::Normalize(cameraToTarget);

	// 右方向は前方向に対して垂直（外積を使用）
	Vector3 up = { 0.0f, 1.0f, 0.0f };
	currentCameraRight_ = Vector::Normalize(Vector::Cross(up, currentCameraForward_));
}

void GameCameraController::SetCameraPositionAndTarget(const Vector3& position, const Vector3& target) {
	// Camera型にキャスト（ICamera -> Camera）
	Camera* camera = static_cast<Camera*>(camera_);
	if (!camera) return;

	// カメラの位置を設定
	camera->SetTranslate(position);

	// LookAt行列を計算してビュー行列として設定
	Vector3 up = { 0.0f, 1.0f, 0.0f };
	Matrix4x4 viewMatrix = Matrix::LookAt(position, target, up);
	camera->SetViewMatrix(viewMatrix);

	// カメラの行列を更新
	camera->UpdateMatrix();
	camera->TransferMatrix();
}

void GameCameraController::StartBossDefeatSequence() {
// ボス撃破演出を開始
currentState_ = CameraState::BossDefeatFocus;

// 現在のカメラ位置と注視点を保存
transitionStartPosition_ = camera_->GetPosition();
Vector3 playerPosition = player_->GetTransform().translate;
transitionStartTarget_ = Vector::Add(playerPosition, Vector3{ 0.0f, 1.0f, 0.0f });

// ボス撃破タイマーを開始
	bossDefeatTimer_.Start(bossDefeatFocusDuration_, false);

	// タイマー終了時のコールバック
	bossDefeatTimer_.SetOnComplete([this]() {
		// 撃破演出終了コールバックを呼び出し
		if (onBossDefeatFinishedCallback_) {
			onBossDefeatFinishedCallback_();
		}
		});
}

void GameCameraController::UpdateBossDefeatFocus() {
	// ボスにフォーカスする位置へイージング補間で移動
	float t = bossDefeatTimer_.GetElapsedTime() / bossDefeatFocusDuration_;

	// 目標位置と注視点を計算
	Vector3 bossPosition = boss_->GetTransform().translate;
	Vector3 targetCameraPosition = Vector::Add(bossPosition, bossDefeatCameraOffset_);
	// ボスの中心（高さ2.0の位置）を注視
	Vector3 targetLookAt = Vector::Add(bossPosition, Vector3{ 0.0f, 1.5f, 0.0f });

	// イージングを適用して補間
	Vector3 currentPosition = EasingUtil::LerpVector3(
		transitionStartPosition_,
		targetCameraPosition,
		t,
		easingType_
	);

	Vector3 currentTarget = EasingUtil::LerpVector3(
		transitionStartTarget_,
		targetLookAt,
		t,
		easingType_
	);

	SetCameraPositionAndTarget(currentPosition, currentTarget);
}

bool GameCameraController::IsBossDefeatFinished() const {
	return currentState_ == CameraState::BossDefeatFocus && !bossDefeatTimer_.IsActive();
}

void GameCameraController::StartPlayerDeathSequence() {
	// プレイヤー死亡演出を開始
	currentState_ = CameraState::PlayerDeathFocus;

	// 現在のカメラ位置と注視点を保存
	transitionStartPosition_ = camera_->GetPosition();
	Vector3 playerPosition = player_->GetTransform().translate;
	transitionStartTarget_ = Vector::Add(playerPosition, Vector3{ 0.0f, 1.0f, 0.0f });

	// プレイヤー死亡タイマーを開始
	playerDeathTimer_.Start(playerDeathFocusDuration_, false);

	// タイマー終了時のコールバック
	playerDeathTimer_.SetOnComplete([this]() {
		// 死亡演出終了コールバックを呼び出し
		if (onPlayerDeathFinishedCallback_) {
			onPlayerDeathFinishedCallback_();
		}
		});
}

void GameCameraController::UpdatePlayerDeathFocus() {
	// プレイヤーにフォーカスする位置へイージング補間で移動
	float t = playerDeathTimer_.GetElapsedTime() / playerDeathFocusDuration_;

	// 目標位置と注視点を計算（プレイヤーを斜めから見下ろす）
	Vector3 playerPosition = player_->GetTransform().translate;
	Vector3 targetCameraPosition = Vector::Add(playerPosition, playerDeathCameraOffset_);

	// プレイヤーの中心（高さ1.0の位置）を注視
	Vector3 targetLookAt = Vector::Add(playerPosition, Vector3{ 0.0f, 1.0f, 0.0f });

	// イージングを適用して補間（スローモーション的な演出）
	Vector3 currentPosition = EasingUtil::LerpVector3(
		transitionStartPosition_,
		targetCameraPosition,
		t,
		EasingUtil::Type::EaseOutCubic  // 死亡演出は減速して終わる
	);

	Vector3 currentTarget = EasingUtil::LerpVector3(
		transitionStartTarget_,
		targetLookAt,
		t,
		EasingUtil::Type::EaseOutCubic
	);

	SetCameraPositionAndTarget(currentPosition, currentTarget);
}

void GameCameraController::UpdateOpeningFadeOut() {
	// フェードアウト中は現在のカメラ位置を維持
	// フェードエフェクトを更新
	auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
	if (postEffectManager) {
		auto* fadeEffect = postEffectManager->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);
		if (fadeEffect) {
			float t = fadeOutTimer_.GetElapsedTime() / fadeDuration_;
			fadeEffect->SetFadeAlpha(t);
		}
	}
}

void GameCameraController::UpdateOpeningFadeIn() {
	// フェードイン中はプレイヤー追従位置を維持
	// フェードエフェクトを更新
	auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
	if (postEffectManager) {
		auto* fadeEffect = postEffectManager->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);
		if (fadeEffect) {
			float t = fadeInTimer_.GetElapsedTime() / fadeDuration_;
			fadeEffect->SetFadeAlpha(1.0f - t);  // 1.0→0.0へフェードイン
		}
	}
}

bool GameCameraController::IsPlayerDeathFinished() const {
	return currentState_ == CameraState::PlayerDeathFocus && !playerDeathTimer_.IsActive();
}

Vector3 GameCameraController::GetCameraForward() const {
	return currentCameraForward_;
}

Vector3 GameCameraController::GetCameraRight() const {
	return currentCameraRight_;
}

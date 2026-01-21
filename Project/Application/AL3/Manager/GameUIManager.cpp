#include "GameUIManager.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/WinApp/WinApp.h"
#include "Engine/ObjectCommon/SpriteObject.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/GameObject/ReticleObject.h"
#include <algorithm>
#include <cmath>

using namespace CoreEngine;

void GameUIManager::Initialize(
	CoreEngine::EngineSystem* engine,
	PlayerObject* player,
	BossObject* boss,
	ReticleObject* reticle
) {
	engine_ = engine;
	player_ = player;
	boss_ = boss;
	reticle_ = reticle;
}

void GameUIManager::InitializeBossName(std::function<CoreEngine::SpriteObject* (const std::string&, const std::string&)> createSpriteCallback) {
	if (!createSpriteCallback) return;

	// ボス名画像を画面上部中央に配置
	bossNameSprite_ = createSpriteCallback("Assets/Texture/bossName.png", "BossName");
	bossNameSprite_->SetActive(true);
	bossNameSprite_->GetSpriteTransform().translate = { 0.0f, 321.0f, 0.0f };
	bossNameSprite_->GetSpriteTransform().scale = { 0.6f, 0.6f, 1.0f };
}

void GameUIManager::InitializeBossHPBar(std::function<CoreEngine::SpriteObject* (const std::string&, const std::string&)> createSpriteCallback) {
	if (!createSpriteCallback) return;

	// 枠の太さ
	constexpr float FRAME_PADDING = 3.0f;  // 4 → 3に縮小

	// HPバー背景（黒）- 枠として少し大きく
	bossHPBarBackground_ = createSpriteCallback("Assets/Texture/white2x2.png", "BossHPBarBackground");
	bossHPBarBackground_->SetActive(true);
	bossHPBarBackground_->GetSpriteTransform().translate = { 0.0f, HP_BAR_Y, 0.1f };  // Z=0.1で一番奥
	bossHPBarBackground_->GetSpriteTransform().scale = { HP_BAR_WIDTH + FRAME_PADDING * 2, HP_BAR_HEIGHT + FRAME_PADDING * 2, 1.0f };
	bossHPBarBackground_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });  // 黒色に変更

	// HPバー遅延（赤・ダメージ表示用）- 背景と前景の間に配置
	bossHPBarDelay_ = createSpriteCallback("Assets/Texture/white2x2.png", "BossHPBarDelay");
	bossHPBarDelay_->SetActive(true);
	bossHPBarDelay_->GetSpriteTransform().translate = { 0.0f, HP_BAR_Y, 0.05f };  // Z=0.05で中間
	bossHPBarDelay_->GetSpriteTransform().scale = { HP_BAR_WIDTH, HP_BAR_HEIGHT, 1.0f };
	bossHPBarDelay_->SetColor({ 1.0f, 0.2f, 0.2f, 1.0f });  // より鮮やかで明るい赤に変更

	// HPバー前景（緑）
	bossHPBarForeground_ = createSpriteCallback("Assets/Texture/white2x2.png", "BossHPBarForeground");
	bossHPBarForeground_->SetActive(true);
	bossHPBarForeground_->GetSpriteTransform().translate = { 0.0f, HP_BAR_Y, 0.0f };  // Z=0.0で一番手前
	bossHPBarForeground_->GetSpriteTransform().scale = { HP_BAR_WIDTH, HP_BAR_HEIGHT, 1.0f };
	bossHPBarForeground_->SetColor({ 0.1f, 0.9f, 0.1f, 1.0f });  // より鮮やかな緑に変更

	// 初期値として遅延HPを最大に設定
	delayedHPRatio_ = 1.0f;
}

void GameUIManager::InitializePlayerHPBar(std::function<CoreEngine::SpriteObject* (const std::string&, const std::string&)> createSpriteCallback) {
	if (!createSpriteCallback) return;

	// 枠の太さ
	constexpr float FRAME_PADDING = 3.0f;

	// プレイヤーHPバー背景（黒）- 枠として少し大きく
	playerHPBarBackground_ = createSpriteCallback("Assets/Texture/white1x1.png", "PlayerHPBarBackground");
	playerHPBarBackground_->SetActive(true);
	playerHPBarBackground_->GetSpriteTransform().translate = { 0.0f, PLAYER_HP_BAR_Y, 0.1f };  // Z=0.1で一番奥
	playerHPBarBackground_->GetSpriteTransform().scale = { PLAYER_HP_BAR_WIDTH + FRAME_PADDING * 2, HP_BAR_HEIGHT + FRAME_PADDING * 2, 1.0f };
	playerHPBarBackground_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });  // 黒色

	// プレイヤーHPバー遅延（赤・ダメージ表示用）- 背景と前景の間に配置
	playerHPBarDelay_ = createSpriteCallback("Assets/Texture/white1x1.png", "PlayerHPBarDelay");
	playerHPBarDelay_->SetActive(true);
	playerHPBarDelay_->GetSpriteTransform().translate = { 0.0f, PLAYER_HP_BAR_Y, 0.05f };  // Z=0.05で中間
	playerHPBarDelay_->GetSpriteTransform().scale = { PLAYER_HP_BAR_WIDTH, HP_BAR_HEIGHT, 1.0f };
	playerHPBarDelay_->SetColor({ 1.0f, 0.2f, 0.2f, 1.0f });  // 鮮やかな赤

	// プレイヤーHPバー前景（緑）
	playerHPBarForeground_ = createSpriteCallback("Assets/Texture/white1x1.png", "PlayerHPBarForeground");
	playerHPBarForeground_->SetActive(true);
	playerHPBarForeground_->GetSpriteTransform().translate = { 0.0f, PLAYER_HP_BAR_Y, 0.0f };  // Z=0.0で一番手前
	playerHPBarForeground_->GetSpriteTransform().scale = { PLAYER_HP_BAR_WIDTH, HP_BAR_HEIGHT, 1.0f };
	playerHPBarForeground_->SetColor({ 0.1f, 0.9f, 0.1f, 1.0f });  // 鮮やかな緑

	// HPテキストスプライト（ゲージの上、左寄せ）
	playerHPText_ = createSpriteCallback("Assets/AppAssets/Texture/hp.png", "PlayerHPText");
	playerHPText_->SetActive(true);
	// ゲージの左端に配置（ゲージの幅の半分 + テキスト幅の半分を左にオフセット）
	constexpr float HP_TEXT_OFFSET_X = -PLAYER_HP_BAR_WIDTH / 2.0f + 30.0f;  // 左端から30ピクセル右
	constexpr float HP_TEXT_OFFSET_Y = 25.0f;  // ゲージの25ピクセル上
	playerHPText_->GetSpriteTransform().translate = { HP_TEXT_OFFSET_X, PLAYER_HP_BAR_Y + HP_TEXT_OFFSET_Y, 0.0f };
	playerHPText_->GetSpriteTransform().scale = { 0.5f, 0.5f, 1.0f };  // 適度なサイズに縮小

	// 初期値として遅延HPを最大に設定
	playerDelayedHPRatio_ = 1.0f;
}

void GameUIManager::InitializeControlUI(std::function<CoreEngine::SpriteObject* (const std::string&, const std::string&)> createSpriteCallback) {
	if (!createSpriteCallback) return;

	// プレイヤー移動UI（左下）
	playerMoveSprite_ = createSpriteCallback("Assets/AppAssets/Texture/playerMove.png", "PlayerMove");
	playerMoveSprite_->SetActive(true);
	playerMoveSprite_->GetSpriteTransform().translate = { -520.0f, -310.0f, 0.0f };  // 左下
	playerMoveSprite_->GetSpriteTransform().scale = { 0.5f, 0.5f, 1.0f };  // スケールを小さく

	// レティクル移動UI（右下）
	reticleMoveSprite_ = createSpriteCallback("Assets/AppAssets/Texture/reticleMove.png", "ReticleMove");
	reticleMoveSprite_->SetActive(true);
	reticleMoveSprite_->GetSpriteTransform().translate = { 455.0f, -313.0f, 0.0f };  // 右下
	reticleMoveSprite_->GetSpriteTransform().scale = { 0.4f, 0.4f, 1.0f };  // スケールを小さく

	// 弾発射UI（右下、レティクル移動の上）
	bulletShotSprite_ = createSpriteCallback("Assets/AppAssets/Texture/bulletShot.png", "BulletShot");
	bulletShotSprite_->SetActive(true);
	bulletShotSprite_->GetSpriteTransform().translate = { 526.0f, -200.0f, 0.0f };  // 右下、レティクルの上
	bulletShotSprite_->GetSpriteTransform().scale = { 0.5f, 0.5f, 1.0f };  // スケールを小さく

	// ===== Xboxコントローラー操作UI =====

	// 左スティックUI（左下） - プレイヤー移動用（playerMoveの上に配置）
	// 横移動と縦移動を同じ座標に重ねる
	xboxStickLHorizontalSprite_ = createSpriteCallback("Assets/AppAssets/Texture/xbox_stick_l_horizontal.png", "XboxStickLHorizontal");
	xboxStickLHorizontalSprite_->SetActive(true);
	xboxStickLHorizontalSprite_->GetSpriteTransform().translate = { -517.0f, -247.0f, 0.0f };  // playerMoveの100ピクセル上
	xboxStickLHorizontalSprite_->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };

	xboxStickLVerticalSprite_ = createSpriteCallback("Assets/AppAssets/Texture/xbox_stick_l_vertical.png", "XboxStickLVertical");
	xboxStickLVerticalSprite_->SetActive(true);
	xboxStickLVerticalSprite_->GetSpriteTransform().translate = { -517.0f, -247.0f, 0.0f };  // 左スティック横移動と同じ座標
	xboxStickLVerticalSprite_->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };

	// 右スティックUI（右下） - レティクル移動用（reticleMoveの上に配置）
	// 横移動と縦移動を同じ座標に重ねる
	xboxStickRHorizontalSprite_ = createSpriteCallback("Assets/AppAssets/Texture/xbox_stick_r_horizontal.png", "XboxStickRHorizontal");
	xboxStickRHorizontalSprite_->SetActive(true);
	xboxStickRHorizontalSprite_->GetSpriteTransform().translate = { 455.0f, -257.0f, 0.0f };  // reticleMoveの100ピクセル上
	xboxStickRHorizontalSprite_->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };

	xboxStickRVerticalSprite_ = createSpriteCallback("Assets/AppAssets/Texture/xbox_stick_r_vertical.png", "XboxStickRVertical");
	xboxStickRVerticalSprite_->SetActive(true);
	xboxStickRVerticalSprite_->GetSpriteTransform().translate = { 455.0f, -257.0f, 0.0f };  // 右スティック横移動と同じ座標
	xboxStickRVerticalSprite_->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };

	// RTボタンUI（右下、bulletShotの上に配置）
	xboxRbSprite_ = createSpriteCallback("Assets/AppAssets/Texture/xbox_rt.png", "XboxRb");
	xboxRbSprite_->SetActive(true);  // 常に表示
	xboxRbSprite_->GetSpriteTransform().translate = { 545.0f, -143.0f, 0.0f };
	xboxRbSprite_->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };
}

void GameUIManager::InitializeResultSprites(std::function<CoreEngine::SpriteObject* (const std::string&, const std::string&)> createSpriteCallback) {
	if (!createSpriteCallback) return;

	// 勝利スプライトの初期化
	winSprite_ = createSpriteCallback("Assets/AppAssets/Texture/win.png", "WinSprite");
	winSprite_->SetActive(false);  // 初期は非表示
	winSprite_->GetSpriteTransform().translate = { 0.0f, RESULT_START_Y, 0.0f };
	winSprite_->GetSpriteTransform().scale = { 0.5f, 0.5f, 1.0f };
	winSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });  // 初期は透明

	// 敗北スプライトの初期化
	loseSprite_ = createSpriteCallback("Assets/AppAssets/Texture/lose.png", "LoseSprite");
	loseSprite_->SetActive(false);  // 初期は非表示
	loseSprite_->GetSpriteTransform().translate = { 0.0f, RESULT_START_Y, 0.0f };
	loseSprite_->GetSpriteTransform().scale = { 0.5f, 0.5f, 1.0f };
	loseSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });  // 初期は透明
}

void GameUIManager::Update() {
	if (!boss_ || !bossHPBarForeground_ || !bossHPBarDelay_) {
		return;
	}

	// deltaTimeを取得
	auto frameRateController = engine_->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) {
		return;
	}
	float deltaTime = frameRateController->GetDeltaTime();

	// ===== リザルト演出の更新 =====
	UpdateResultAnimation(deltaTime);

	// ===== ボスHPバーの更新 =====
	UpdateBossHPBar(deltaTime);

	// ===== プレイヤーHPバーの更新 =====
	UpdatePlayerHPBar(deltaTime);
}

void GameUIManager::UpdateBossHPBar(float deltaTime) {
	if (!boss_ || !bossHPBarForeground_ || !bossHPBarDelay_) {
		return;
	}

	// UI表示演出中の処理
	if (isUIAnimating_) {
		uiAnimationTimer_ += deltaTime;
		float progress = uiAnimationTimer_ / UI_ANIMATION_DURATION;
		hpBarAnimationProgress_ = (progress < 1.0f) ? progress : 1.0f;

		// === フェーズ1: 背景バーの回転演出（0.0〜0.5秒） ===
		float rotationProgress = (uiAnimationTimer_ < HP_BAR_ROTATION_DURATION)
			? (uiAnimationTimer_ / HP_BAR_ROTATION_DURATION)
			: 1.0f;

		// 回転のイージング（EaseOutCubic）
		float rotationEasedT = 1.0f - std::pow(1.0f - rotationProgress, 3.0f);

		// 背景バーの回転と移動（高速回転）
		float currentRotation = hpBarInitialRotation_ * (1.0f - rotationEasedT);
		float currentY = hpBarInitialY_ + (HP_BAR_Y - hpBarInitialY_) * rotationEasedT;

		// 背景バー（黒）のみ回転
		if (bossHPBarBackground_) {
			bossHPBarBackground_->GetSpriteTransform().translate.y = currentY;
			bossHPBarBackground_->GetSpriteTransform().rotate.z = currentRotation;
		}

		// === フェーズ2: HPゲージの増加演出（0.5秒後から1.5秒まで） ===
		float fillStartTime = HP_BAR_ROTATION_DURATION;
		float fillProgress = 0.0f;

		if (uiAnimationTimer_ > fillStartTime) {
			float fillElapsed = uiAnimationTimer_ - fillStartTime;
			fillProgress = (fillElapsed < HP_FILL_DURATION)
				? (fillElapsed / HP_FILL_DURATION)
				: 1.0f;
		}

		// HP増加のイージング（EaseOutQuad - よりゆっくり）
		float fillEasedT = 1.0f - std::pow(1.0f - fillProgress, 2.0f);

		// 緑と赤のHPバーは回転せず、Y座標のみ追従
		if (bossHPBarDelay_) {
			bossHPBarDelay_->GetSpriteTransform().translate.y = currentY;
			bossHPBarDelay_->GetSpriteTransform().rotate.z = 0.0f;  // 回転なし
		}
		if (bossHPBarForeground_) {
			bossHPBarForeground_->GetSpriteTransform().translate.y = currentY;
			bossHPBarForeground_->GetSpriteTransform().rotate.z = 0.0f;  // 回転なし
		}

		// ボス名の演出（フェードイン）
		if (bossNameSprite_) {
			float alpha = rotationEasedT;
			bossNameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
		}

		// 演出終了判定
		if (hpBarAnimationProgress_ >= 1.0f) {
			isUIAnimating_ = false;
			// 回転をリセット
			if (bossHPBarBackground_) bossHPBarBackground_->GetSpriteTransform().rotate.z = 0.0f;
			if (bossHPBarDelay_) bossHPBarDelay_->GetSpriteTransform().rotate.z = 0.0f;
			if (bossHPBarForeground_) bossHPBarForeground_->GetSpriteTransform().rotate.z = 0.0f;
		}

		// HPゲージの増加演出を適用
		fillProgress = fillEasedT;  // ローカル変数をメンバに反映（次のセクションで使用）
	}

	// 現在のHP割合を取得
	float currentHPRatio = boss_->GetHPRatio();

	// UI表示演出中はHPバーを徐々に増やす
	float displayHPRatio = currentHPRatio;
	if (isUIAnimating_) {
		// 回転完了後からHP増加開始
		float fillStartTime = HP_BAR_ROTATION_DURATION;
		float fillProgress = 0.0f;

		if (uiAnimationTimer_ > fillStartTime) {
			float fillElapsed = uiAnimationTimer_ - fillStartTime;
			fillProgress = (fillElapsed < HP_FILL_DURATION)
				? (fillElapsed / HP_FILL_DURATION)
				: 1.0f;

			// HP増加のイージング（EaseOutQuad）
			float fillEasedT = 1.0f - std::pow(1.0f - fillProgress, 2.0f);
			displayHPRatio = currentHPRatio * fillEasedT;
		} else {
			// 回転中はHPを0に
			displayHPRatio = 0.0f;
		}
	}

	// 遅延HPを更新（常に現在HPと比較して大きい方を保持し、徐々に減少）
	if (delayedHPRatio_ > currentHPRatio) {
		// 現在のHPとの差分を計算
		float hpDifference = delayedHPRatio_ - currentHPRatio;

		// 差分が大きい場合は速く、小さい場合は遅く減少させる（段階的な減速）
		float decaySpeed;
		if (hpDifference > HP_DELAY_THRESHOLD) {
			// 差が大きい時は速めに減少
			decaySpeed = HP_DELAY_SPEED_FAST;
		} else {
			// 差が小さくなったらゆっくり減少（ダメージ量を確認しやすく）
			decaySpeed = HP_DELAY_SPEED_SLOW;
		}

		// 遅延HPを減少
		delayedHPRatio_ -= decaySpeed * deltaTime;

		// 現在のHPを下回らないようにクランプ
		if (delayedHPRatio_ < currentHPRatio) {
			delayedHPRatio_ = currentHPRatio;
		}
	}

	// UI表示演出中は遅延HPも演出に合わせる
	float displayDelayedHPRatio = delayedHPRatio_;
	if (isUIAnimating_) {
		// 回転完了後からHP増加開始
		float fillStartTime = HP_BAR_ROTATION_DURATION;
		float fillProgress = 0.0f;

		if (uiAnimationTimer_ > fillStartTime) {
			float fillElapsed = uiAnimationTimer_ - fillStartTime;
			fillProgress = (fillElapsed < HP_FILL_DURATION)
				? (fillElapsed / HP_FILL_DURATION)
				: 1.0f;

			// HP増加のイージング（EaseOutQuad）
			float fillEasedT = 1.0f - std::pow(1.0f - fillProgress, 2.0f);
			displayDelayedHPRatio = delayedHPRatio_ * fillEasedT;
		} else {
			// 回転中はHPを0に
			displayDelayedHPRatio = 0.0f;
		}
	}

	// 前景バー（緑）の更新
	bossHPBarForeground_->GetSpriteTransform().scale.x = HP_BAR_WIDTH * displayHPRatio;
	float foregroundOffset = HP_BAR_WIDTH * (1.0f - displayHPRatio) * -0.5f;
	bossHPBarForeground_->GetSpriteTransform().translate.x = foregroundOffset;

	// 遅延バー（赤）の更新
	bossHPBarDelay_->GetSpriteTransform().scale.x = HP_BAR_WIDTH * displayDelayedHPRatio;
	float delayOffset = HP_BAR_WIDTH * (1.0f - displayDelayedHPRatio) * -0.5f;
	bossHPBarDelay_->GetSpriteTransform().translate.x = delayOffset;
}

void GameUIManager::UpdatePlayerHPBar(float deltaTime) {
	if (!player_ || !playerHPBarForeground_ || !playerHPBarDelay_) {
		return;
	}

	// 現在のHP割合を取得
	float currentHPRatio = player_->GetHPRatio();

	// UI表示演出中はHPバーを徐々に増やす
	float displayHPRatio = currentHPRatio;
	if (isUIAnimating_) {
		// 回転完了後からHP増加開始
		float fillStartTime = HP_BAR_ROTATION_DURATION;
		float fillProgress = 0.0f;

		if (uiAnimationTimer_ > fillStartTime) {
			float fillElapsed = uiAnimationTimer_ - fillStartTime;
			fillProgress = (fillElapsed < HP_FILL_DURATION)
				? (fillElapsed / HP_FILL_DURATION)
				: 1.0f;

			// HP増加のイージング（EaseOutQuad）
			float fillEasedT = 1.0f - std::pow(1.0f - fillProgress, 2.0f);
			displayHPRatio = currentHPRatio * fillEasedT;
		} else {
			// 回転中はHPを0に
			displayHPRatio = 0.0f;
		}
	}

	// 遅延HPを更新（常に現在HPと比較して大きい方を保持し、徐々に減少）
	if (playerDelayedHPRatio_ > currentHPRatio) {
		// 現在のHPとの差分を計算
		float hpDifference = playerDelayedHPRatio_ - currentHPRatio;

		// 差分が大きい場合は速く、小さい場合は遅く減少させる（段階的な減速）
		float decaySpeed;
		if (hpDifference > HP_DELAY_THRESHOLD) {
			// 差が大きい時は速めに減少
			decaySpeed = HP_DELAY_SPEED_FAST;
		} else {
			// 差が小さくなったらゆっくり減少（ダメージ量を確認しやすく）
			decaySpeed = HP_DELAY_SPEED_SLOW;
		}

		// 遅延HPを減少
		playerDelayedHPRatio_ -= decaySpeed * deltaTime;

		// 現在のHPを下回らないようにクランプ
		if (playerDelayedHPRatio_ < currentHPRatio) {
			playerDelayedHPRatio_ = currentHPRatio;
		}
	}

	// UI表示演出中は遅延HPも演出に合わせる
	float displayDelayedHPRatio = playerDelayedHPRatio_;
	if (isUIAnimating_) {
		// 回転完了後からHP増加開始
		float fillStartTime = HP_BAR_ROTATION_DURATION;
		float fillProgress = 0.0f;

		if (uiAnimationTimer_ > fillStartTime) {
			float fillElapsed = uiAnimationTimer_ - fillStartTime;
			fillProgress = (fillElapsed < HP_FILL_DURATION)
				? (fillElapsed / HP_FILL_DURATION)
				: 1.0f;

			// HP増加のイージング（EaseOutQuad）
			float fillEasedT = 1.0f - std::pow(1.0f - fillProgress, 2.0f);
			displayDelayedHPRatio = playerDelayedHPRatio_ * fillEasedT;
		} else {
			// 回転中はHPを0に
			displayDelayedHPRatio = 0.0f;
		}
	}

	// 前景バー（緑）の更新
	playerHPBarForeground_->GetSpriteTransform().scale.x = PLAYER_HP_BAR_WIDTH * displayHPRatio;
	float foregroundOffset = PLAYER_HP_BAR_WIDTH * (1.0f - displayHPRatio) * -0.5f;
	playerHPBarForeground_->GetSpriteTransform().translate.x = foregroundOffset;

	// 遅延バー（赤）の更新
	playerHPBarDelay_->GetSpriteTransform().scale.x = PLAYER_HP_BAR_WIDTH * displayDelayedHPRatio;
	float delayOffset = PLAYER_HP_BAR_WIDTH * (1.0f - displayDelayedHPRatio) * -0.5f;
	playerHPBarDelay_->GetSpriteTransform().translate.x = delayOffset;
}

void GameUIManager::UpdateResultAnimation(float deltaTime) {
	if (!isResultAnimating_) return;

	resultAnimationTimer_ += deltaTime;
	float progress = resultAnimationTimer_ / RESULT_ANIMATION_DURATION;

	if (progress > 1.0f) {
		progress = 1.0f;
		isResultAnimating_ = false;
	}

	// イージング（EaseOutCubic）
	float easedProgress = 1.0f - std::pow(1.0f - progress, 3.0f);

	// Y座標を補間（画面中央やや上から画面中央へ）
	float currentY = RESULT_START_Y + (RESULT_TARGET_Y - RESULT_START_Y) * easedProgress;

	// アルファ値を補間（透明から不透明へ）
	float alpha = easedProgress;

	// 勝利スプライトが有効な場合
	if (winSprite_ && winSprite_->IsActive()) {
		winSprite_->GetSpriteTransform().translate.y = currentY;
		winSprite_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
	}

	// 敗北スプライトが有効な場合
	if (loseSprite_ && loseSprite_->IsActive()) {
		loseSprite_->GetSpriteTransform().translate.y = currentY;
		loseSprite_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
	}
}

void GameUIManager::StartWinAnimation() {
	// 勝利演出を開始
	isResultAnimating_ = true;
	resultAnimationTimer_ = 0.0f;

	// 勝利スプライトを表示
	if (winSprite_) {
		winSprite_->SetActive(true);
		winSprite_->GetSpriteTransform().translate.y = RESULT_START_Y;
		winSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}

	// 敗北スプライトは非表示
	if (loseSprite_) {
		loseSprite_->SetActive(false);
	}
}

void GameUIManager::StartLoseAnimation() {
	// 敗北演出を開始
	isResultAnimating_ = true;
	resultAnimationTimer_ = 0.0f;

	// 敗北スプライトを表示
	if (loseSprite_) {
		loseSprite_->SetActive(true);
		loseSprite_->GetSpriteTransform().translate.y = RESULT_START_Y;
		loseSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}

	// 勝利スプライトは非表示
	if (winSprite_) {
		winSprite_->SetActive(false);
	}
}

void GameUIManager::SetUIVisible(bool visible) {
	// レティクルの表示/非表示
	if (reticle_) {
		reticle_->SetVisible(visible);
	}

	// ボス名の表示/非表示
	if (bossNameSprite_) {
		bossNameSprite_->SetActive(visible);
	}

	// ボスHPバーの表示/非表示
	if (bossHPBarBackground_) {
		bossHPBarBackground_->SetActive(visible);
	}
	if (bossHPBarDelay_) {
		bossHPBarDelay_->SetActive(visible);
	}
	if (bossHPBarForeground_) {
		bossHPBarForeground_->SetActive(visible);
	}

	// プレイヤーHPバーの表示/非表示
	if (playerHPBarBackground_) {
		playerHPBarBackground_->SetActive(visible);
	}
	if (playerHPBarDelay_) {
		playerHPBarDelay_->SetActive(visible);
	}
	if (playerHPBarForeground_) {
		playerHPBarForeground_->SetActive(visible);
	}
	if (playerHPText_) {
		playerHPText_->SetActive(visible);
	}

	// 操作方法UIの表示/非表示
	if (playerMoveSprite_) {
		playerMoveSprite_->SetActive(visible);
	}
	if (reticleMoveSprite_) {
		reticleMoveSprite_->SetActive(visible);
	}
	if (bulletShotSprite_) {
		bulletShotSprite_->SetActive(visible);
	}

	// Xboxコントローラー操作UIの表示/非表示
	if (xboxRbSprite_) {
		xboxRbSprite_->SetActive(visible);  // 他のUIと同じように表示/非表示
	}

	if (xboxStickLHorizontalSprite_) {
		xboxStickLHorizontalSprite_->SetActive(visible);
	}
	if (xboxStickLVerticalSprite_) {
		xboxStickLVerticalSprite_->SetActive(visible);
	}
	if (xboxStickRHorizontalSprite_) {
		xboxStickRHorizontalSprite_->SetActive(visible);
	}
	if (xboxStickRVerticalSprite_) {
		xboxStickRVerticalSprite_->SetActive(visible);
	}
}

void GameUIManager::StartUIShowAnimation() {
	// UI表示演出を開始
	isUIAnimating_ = true;
	uiAnimationTimer_ = 0.0f;
	hpBarAnimationProgress_ = 0.0f;

	// HPバーの初期位置と回転を設定
	hpBarInitialY_ = 0.0f;  // 画面中央
	hpBarInitialRotation_ = 3.14159f * 8.0f;  // 1440度回転（4回転）- より高速に

	// UIを表示状態にする
	SetUIVisible(true);

	// ボス名の初期アルファを0に設定
	if (bossNameSprite_) {
		bossNameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
}

void GameUIManager::UpdateRBInput(bool isPressed) {
	// UpdateRBInputは呼ばれるが何もしない（RBスプライトは常に表示）
	(void)isPressed;  // 未使用パラメータの警告を抑制
}

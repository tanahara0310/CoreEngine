#include "TitleScene.h"
#include "Engine/Input/KeyboardInput.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/GamepadInput.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Audio/SoundManager.h"
#include "Engine/Math/Easing/EasingUtil.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/Release/Camera.h"
#include "Engine/Graphics/Light/LightManager.h"
#include "Scene/SceneManager.h"
#include "WinApp/WinApp.h"
#include <cmath>

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

using namespace CoreEngine;

void TitleScene::Initialize(CoreEngine::EngineSystem* engine)
{
	// 基底クラスの初期化（カメラ、ライト、グリッド等のセットアップ）
	BaseScene::Initialize(engine);

	// カメラの初期化
	InitializeCamera();

	// ライトの初期化
	InitializeLight();

	// タイトルロゴの初期化
	InitializeTitleLogo();

	// メニューボタンの初期化
	InitializeMenuButtons();

	// 壁モデルの初期化
	InitializeWallModel();

	// サウンドの初期化
	InitializeSound();

	// 初期選択はスタート
	selectedMenuIndex_ = 0;
	animationTime_ = 0.0f;
}

void TitleScene::InitializeCamera()
{
	if (!cameraManager_) return;

	CoreEngine::ICamera* releaseCamera = cameraManager_->GetCamera("Release");
	if (!releaseCamera) return;

	Camera* cam = static_cast<Camera*>(releaseCamera);
	cam->SetTranslate({ 2.30f, 4.70f, -85.2f });
	cam->SetRotate({ 0.0f, 0.0f, 0.0f });  // 正面を向く
	cam->UpdateMatrix();
	cam->TransferMatrix();
}

void TitleScene::InitializeLight()
{
	auto lightManager = engine_->GetComponent<LightManager>();
	if (!lightManager) return;

	// ディレクショナルライトの向きを変更（Y方向を上向きに）
	if (directionalLight_) {
		directionalLight_->direction = MathCore::Vector::Normalize({ 0.0f, 1.0f, -0.2f });
	}
}

void TitleScene::InitializeTitleLogo()
{
	auto sprite = CreateObject<SpriteObject>();
	sprite->Initialize("AppAssets/Texture/title.png");
	sprite->GetSpriteTransform().translate = { 0.0f, 150.0f, 0.0f };
	sprite->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };
	// ロゴを初期状態で透明に設定
	sprite->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	sprite->SetActive(true);
	titleSprite_ = sprite;

	// ロゴフェードインタイマーを開始
	logoFadeTimer_.Start(kLogoFadeDuration, false);
}

void TitleScene::InitializeMenuButtons()
{
	// スタートボタンの初期化
	auto startSprite = CreateObject<SpriteObject>();
	startSprite->Initialize("AppAssets/Texture/stert.png");
	startSprite->GetSpriteTransform().translate = { kStartButtonStartX, kStartButtonTargetY, 0.0f };
	startSprite->GetSpriteTransform().scale = { 0.7f, 0.7f, 1.0f };
	startSprite->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	startSprite->SetActive(true);
	startSprite_ = startSprite;

	// やめるボタンの初期化
	auto exitSprite = CreateObject<SpriteObject>();
	exitSprite->Initialize("AppAssets/Texture/exit.png");
	exitSprite->GetSpriteTransform().translate = { kExitButtonStartX, kExitButtonTargetY, 0.0f };
	exitSprite->GetSpriteTransform().scale = { 0.7f, 0.7f, 1.0f };
	exitSprite->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	exitSprite->SetActive(true);
	exitSprite_ = exitSprite;
}

void TitleScene::InitializeWallModel()
{
	auto wall = CreateObject<WallObject>();
	wall->Initialize({ 0.0f, 5.0f, 5.0f }, 0.0f, { 1.0f, 1.0f, 1.0f });
	wallObject_ = wall;
}

void TitleScene::InitializeSound()
{
	auto soundManager = engine_->GetComponent<CoreEngine::SoundManager>();
	if (!soundManager) return;

	selectSE_ = soundManager->CreateSoundResource("Audio/SE/sentaku.mp3");
	decideSE_ = soundManager->CreateSoundResource("Audio/SE/stert.mp3");
	titleBGM_ = soundManager->CreateSoundResource("Audio/BGM/titleScene.mp3");

	selectSE_->SetVolume(0.3f);
	decideSE_->SetVolume(0.3f);

	// BGMの音量を設定してループ再生開始
	if (titleBGM_ && titleBGM_->IsValid()) {
		titleBGM_->SetVolume(0.3f);
		titleBGM_->Play(true);
		
		// BGMをシーンに登録してトランジション時の自動フェードを有効化
		RegisterSceneBGM(&titleBGM_);
	}
}

void TitleScene::OnUpdate()
{
	auto keyboard = engine_->GetComponent<CoreEngine::KeyboardInput>();
	auto inputManager = engine_->GetComponent<CoreEngine::InputManager>();
	if (!keyboard) return;

	auto frameRate = engine_->GetComponent<CoreEngine::FrameRateController>();
	float deltaTime = frameRate ? frameRate->GetDeltaTime() : 0.016f;

	// ロゴフェードイン処理
	if (UpdateLogoFadeIn(deltaTime)) return;

	// メニュー表示遅延処理
	if (UpdateMenuDelay(deltaTime)) return;

	// メニューボタンアニメーション処理
	if (UpdateMenuButtonAnimation(deltaTime)) return;

	// メニュー操作処理
	UpdateMenuInput(keyboard, inputManager, deltaTime);

	// メニュースケールアニメーション処理
	UpdateMenuScaleAnimation(deltaTime);

	// メニュー決定処理
	UpdateMenuDecision(keyboard, inputManager);
}

bool TitleScene::UpdateLogoFadeIn(float deltaTime)
{
	if (!logoFadeTimer_.IsActive()) return false;

	logoFadeTimer_.Update(deltaTime);

	// イージングを適用した進行度を取得（EaseOutCubicで滑らかに）
	float alpha = logoFadeTimer_.GetEasedProgress(EasingUtil::Type::EaseOutCubic);

	// ロゴの色を更新
	if (titleSprite_) {
		titleSprite_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
	}

	// フェード完了時に遅延タイマーを開始
	if (logoFadeTimer_.IsFinished() && !menuDelayTimer_.IsActive()) {
		menuDelayTimer_.Start(kMenuAppearDelay, false);
	}

	return true;  // フェード中はメニュー操作を無効化
}

bool TitleScene::UpdateMenuDelay(float deltaTime)
{
	if (!menuDelayTimer_.IsActive()) return false;

	menuDelayTimer_.Update(deltaTime);

	// 遅延完了時にボタンのアニメーションを開始
	if (menuDelayTimer_.IsFinished()) {
		if (!startButtonTimer_.IsActive()) {
			startButtonTimer_.Start(kMenuAnimDuration, false);
		}
		if (!exitButtonTimer_.IsActive()) {
			exitButtonTimer_.Start(kMenuAnimDuration, false);
		}
	}

	return true;
}

bool TitleScene::UpdateMenuButtonAnimation(float deltaTime)
{
	bool isAnimating = false;

	// スタートボタンのスライドイン（左から中央へ、EaseOutBackで少しオーバーシュート）
	if (startButtonTimer_.IsActive()) {
		startButtonTimer_.Update(deltaTime);
		isAnimating = true;

		// イージングを適用した進行度を取得
		float progress = startButtonTimer_.GetEasedProgress(EasingUtil::Type::EaseOutBack);

		// X座標を補間
		float currentX = EasingUtil::Lerp(kStartButtonStartX, kButtonTargetX, progress, EasingUtil::Type::Linear);

		if (startSprite_) {
			startSprite_->GetSpriteTransform().translate.x = currentX;
			startSprite_->SetColor({ 1.0f, 1.0f, 1.0f, progress });
		}
	}

	// やめるボタンのスライドイン（右から中央へ、EaseOutBackで少しオーバーシュート）
	if (exitButtonTimer_.IsActive()) {
		exitButtonTimer_.Update(deltaTime);
		isAnimating = true;

		// イージングを適用した進行度を取得
		float progress = exitButtonTimer_.GetEasedProgress(EasingUtil::Type::EaseOutBack);

		// X座標を補間
		float currentX = EasingUtil::Lerp(kExitButtonStartX, kButtonTargetX, progress, EasingUtil::Type::Linear);

		if (exitSprite_) {
			exitSprite_->GetSpriteTransform().translate.x = currentX;
			exitSprite_->SetColor({ 1.0f, 1.0f, 1.0f, progress });
		}
	}

	// アニメーション中はメニュー操作を無効化
	bool isMenuAnimationComplete = startButtonTimer_.IsFinished() && exitButtonTimer_.IsFinished();
	return !isMenuAnimationComplete;
}

void TitleScene::UpdateMenuInput(CoreEngine::KeyboardInput* keyboard, CoreEngine::InputManager* inputManager, float deltaTime)
{
	(void)deltaTime;  // 未使用パラメータの警告を抑制

	bool menuChanged = false;
	int previousIndex = selectedMenuIndex_;

	// キーボード：W/Sキーでメニュー選択を変更
	if (keyboard->IsKeyTriggered(DIK_W)) {
		selectedMenuIndex_--;
		if (selectedMenuIndex_ < 0) {
			selectedMenuIndex_ = 1;
		}
		menuChanged = true;
	}

	if (keyboard->IsKeyTriggered(DIK_S)) {
		selectedMenuIndex_++;
		if (selectedMenuIndex_ > 1) {
			selectedMenuIndex_ = 0;
		}
		menuChanged = true;
	}

	// ゲームパッド：左スティックの縦入力でメニュー選択
	if (inputManager) {
		auto gamepad = inputManager->GetGamepad();
		if (gamepad && gamepad->IsConnected()) {
			Stick leftStick = gamepad->GetLeftStick();
			
			// スティックの閾値を設定（デッドゾーン対策）
			constexpr float STICK_THRESHOLD = 0.5f;
			
			// 上方向（y > 0.5）
			static bool wasStickUp = false;
			bool isStickUp = leftStick.y > STICK_THRESHOLD;
			
			if (isStickUp && !wasStickUp) {
				selectedMenuIndex_--;
				if (selectedMenuIndex_ < 0) {
					selectedMenuIndex_ = 1;
				}
				menuChanged = true;
			}
			wasStickUp = isStickUp;
			
			// 下方向（y < -0.5）
			static bool wasStickDown = false;
			bool isStickDown = leftStick.y < -STICK_THRESHOLD;
			
			if (isStickDown && !wasStickDown) {
				selectedMenuIndex_++;
				if (selectedMenuIndex_ > 1) {
					selectedMenuIndex_ = 0;
				}
				menuChanged = true;
			}
			wasStickDown = isStickDown;
		}
	}

	// メニューが変更された場合の処理
	if (menuChanged && selectedMenuIndex_ != previousIndex) {
		animationTime_ = 0.0f;

		// 選択SEを再生
		if (selectSE_ && selectSE_->IsValid()) {
			selectSE_->Play(false);
		}
	}
}

void TitleScene::UpdateMenuScaleAnimation(float deltaTime)
{
	// アニメーション時間を更新
	animationTime_ += deltaTime * kAnimationSpeed;

	// スケールアニメーションを計算（sin波で拡縮）
	float scale = kMinScale + (kMaxScale - kMinScale) * (0.5f + 0.5f * std::sin(animationTime_));

	// 選択されているメニューにアニメーションを適用
	if (startSprite_) {
		if (selectedMenuIndex_ == 0) {
			startSprite_->GetSpriteTransform().scale = { scale, scale, 1.0f };
		} else {
			startSprite_->GetSpriteTransform().scale = { kBaseScale, kBaseScale, 1.0f };
		}
	}

	if (exitSprite_) {
		if (selectedMenuIndex_ == 1) {
			exitSprite_->GetSpriteTransform().scale = { scale, scale, 1.0f };
		} else {
			exitSprite_->GetSpriteTransform().scale = { kBaseScale, kBaseScale, 1.0f };
		}
	}
}

void TitleScene::UpdateMenuDecision(CoreEngine::KeyboardInput* keyboard, CoreEngine::InputManager* inputManager)
{
	bool decisionMade = false;

	// キーボード：スペースキーまたはEnterキーで選択を決定
	if (keyboard->IsKeyTriggered(DIK_SPACE) || keyboard->IsKeyTriggered(DIK_RETURN)) {
		decisionMade = true;
	}

	// ゲームパッド：Aボタンで選択を決定
	if (inputManager) {
		auto gamepad = inputManager->GetGamepad();
		if (gamepad && gamepad->IsConnected()) {
			if (gamepad->IsButtonTriggered(GamepadButton::A)) {
				decisionMade = true;
			}
		}
	}

	// 決定処理
	if (decisionMade) {
		// 決定SEを再生
		if (decideSE_ && decideSE_->IsValid()) {
			decideSE_->Play(false);
		}

		if (selectedMenuIndex_ == 0) {
			// スタートを選択 -> ゲームシーンへ遷移
			if (sceneManager_) {
				sceneManager_->ChangeScene("GameScene");
			}
		} else if (selectedMenuIndex_ == 1) {
			// 終了を選択 -> アプリケーション終了
			auto winApp = engine_->GetComponent<CoreEngine::WinApp>();
			if (winApp) {
				PostQuitMessage(0);
			}
		}
	}
}

void TitleScene::Draw()
{
	// 基底クラスの描画
	BaseScene::Draw();
}

void TitleScene::Finalize()
{
	// BGMを停止
	if (titleBGM_ && titleBGM_->IsValid()) {
		titleBGM_->Stop();
	}

	// 基底クラスの終了処理
	BaseScene::Finalize();
}

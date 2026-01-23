#include "GameScene.h"
#include "Engine/Input/KeyboardInput.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/GamepadInput.h"
#include "Scene/SceneManager.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "Application/AL3/GameObject/BulletObject.h"
#include "Application/AL3/GameObject/BossBulletObject.h"
#include "Application/AL3/GameObject/ReticleObject.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/GameObject/JumpAttackHitbox.h"
#include "Application/AL3/GameObject/SkydomeObject.h"
#include "Application/AL3/GameObject/WallObject.h"
#include "Application/AL3/GameObject/FloorObject.h"
#include "Application/AL3/GameObject/CircleShadowObject.h"
#include "Application/AL3/Debug/BossAIDebugUI.h"
#include "Application/AL3/Camera/GameCameraController.h"
#include "Application/AL3/Manager/GamePlayManager.h"
#include "Application/AL3/Manager/GameUIManager.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/PostEffect/PostEffectManager.h"
#include "Engine/Graphics/PostEffect/PostEffectNames.h"
#include "Engine/Graphics/PostEffect/Effect/Vignette.h"
#include "Engine/Graphics/PostEffect/Effect/ColorGrading.h"
#include "Engine/WinApp/WinApp.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Engine/ObjectCommon/SpriteObject.h"
#include "Engine/Collider/CollisionManager.h"
#include "Engine/Collider/AABBCollider.h"
#include "Engine/Audio/SoundManager.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include <dinput.h>
#include <numbers>

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

using namespace CoreEngine;

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImGuiManager.h"
#include "Engine/Utility/Debug/ImGui/SceneViewport.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Graphics/Line/LineDrawable.h"
#include "Engine/Graphics/Line/LineManager.h"
#include "Engine/Collider/SphereCollider.h"
#include "Engine/Collider/Collider.h"


using Vector3 = CoreEngine::Vector3;

#endif

void GameScene::Initialize(CoreEngine::EngineSystem* engine)
{
	// 基底クラスの初期化（カメラ、ライト、グリッド等のセットアップ）
	BaseScene::Initialize(engine);

#ifdef _DEBUG
	// LineRendererPipelineを取得
	auto renderManager = engine_->GetComponent<CoreEngine::RenderManager>();
	if (renderManager) {
		auto renderer = renderManager->GetRenderer(RenderPassType::Line);
		lineRenderer_ = static_cast<CoreEngine::LineRendererPipeline*>(renderer);
	}

	// コライダーデバッグ用のLineDrawableを作成
	colliderDebugLines_ = CreateObject<CoreEngine::LineDrawable>();
	colliderDebugLines_->Initialize(lineRenderer_);
	colliderDebugLines_->SetActive(true);
#endif

	// プレイヤーの初期化
	InitializePlayer();

	// レティクルの初期化
	InitializeReticle();

	// ボスの初期化
	InitializeBoss();

	// 天球の初期化
	InitializeSkydome();

	// 壁の初期化
	InitializeWall();

	// 床の初期化
	InitializeFloor();

	// 丸影の初期化（テスト用）
	InitializeShadows();

	// ポストエフェクトの初期化
	InitializePostEffects();

	// ゲームプレイマネージャーの初期化
	gamePlayManager_ = std::make_unique<GamePlayManager>();
	gamePlayManager_->Initialize(engine_, player_, reticle_, boss_);

	// 衝突判定システムの初期化
	gamePlayManager_->InitializeCollision();

	// パーティクルシステムの初期化
	gamePlayManager_->InitializeParticles([this]() {
		return CreateObject<CoreEngine::ParticleSystem>();
		});

	// 弾生成コールバックを設定
	gamePlayManager_->SetSpawnBulletCallback([this]() {
		SpawnBullet();
		});

	// UIマネージャーの初期化
	uiManager_ = std::make_unique<GameUIManager>();
	uiManager_->Initialize(engine_, player_, boss_, reticle_);

	// ボス名画像の初期化
	uiManager_->InitializeBossName([this](const std::string& texturePath, const std::string& name) {
		auto sprite = CreateObject<CoreEngine::SpriteObject>();
		sprite->Initialize(texturePath, name);
		return sprite;
		});

	// ボスHPバーの初期化
	uiManager_->InitializeBossHPBar([this](const std::string& texturePath, const std::string& name) {
		auto sprite = CreateObject<CoreEngine::SpriteObject>();
		sprite->Initialize(texturePath, name);
		return sprite;
		});

	// プレイヤーHPバーの初期化
	uiManager_->InitializePlayerHPBar([this](const std::string& texturePath, const std::string& name) {
		auto sprite = CreateObject<CoreEngine::SpriteObject>();
		sprite->Initialize(texturePath, name);
		return sprite;
		});

	// 操作方法UIの初期化
	uiManager_->InitializeControlUI([this](const std::string& texturePath, const std::string& name) {
		auto sprite = CreateObject<CoreEngine::SpriteObject>();
		sprite->Initialize(texturePath, name);
		return sprite;
		});

	// リザルトスプライトの初期化
	uiManager_->InitializeResultSprites([this](const std::string& texturePath, const std::string& name) {
		auto sprite = CreateObject<CoreEngine::SpriteObject>();
		sprite->Initialize(texturePath, name);
		return sprite;
		});

	// デバッグUIの初期化
	InitializeDebugUI();

	// カメラコントローラーの初期化
	InitializeCameraController();

	// オープニング演出を開始（UIを非表示にしてから開始）
	if (uiManager_) {
		uiManager_->SetUIVisible(false);  // 演出開始前に全UIを非表示
	}
	
	if (cameraController_) {
		cameraController_->StartOpeningSequence();
	}
}

void GameScene::InitializePlayer() {
	// ModelManagerを取得
	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) {
		return;
	}

	// ===== プレイヤーのアニメーションを読み込む =====

	// 待機アニメーション
	AnimationLoadInfo idleAnimInfo;
	idleAnimInfo.directory = "Assets/AppAssets/Player";
	idleAnimInfo.modelFilename = "player.gltf";
	idleAnimInfo.animationName = "playerIdleAnimation";
	idleAnimInfo.animationFilename = "playerIdle.gltf";
	modelManager->LoadAnimation(idleAnimInfo);

	// 歩くアニメーション
	AnimationLoadInfo walkAnimInfo;
	walkAnimInfo.directory = "Assets/AppAssets/Player";
	walkAnimInfo.modelFilename = "player.gltf";
	walkAnimInfo.animationName = "playerWalkAnimation";
	walkAnimInfo.animationFilename = "player.gltf";
	modelManager->LoadAnimation(walkAnimInfo);

	// 走るアニメーション
	AnimationLoadInfo runAnimInfo;
	runAnimInfo.directory = "Assets/AppAssets/Player";
	runAnimInfo.modelFilename = "player.gltf";
	runAnimInfo.animationName = "playerRunAnimation";
	runAnimInfo.animationFilename = "playerRun.gltf";
	modelManager->LoadAnimation(runAnimInfo);

	// 銃を構えるアニメーション
	AnimationLoadInfo gunAnimInfo;
	gunAnimInfo.directory = "Assets/AppAssets/Player";
	gunAnimInfo.modelFilename = "player.gltf";
	gunAnimInfo.animationName = "playerGunAnimation";
	gunAnimInfo.animationFilename = "playerGun.gltf";
	modelManager->LoadAnimation(gunAnimInfo);

	// 死亡アニメーション
	AnimationLoadInfo dieAnimInfo;
	dieAnimInfo.directory = "Assets/AppAssets/Player";
	dieAnimInfo.modelFilename = "player.gltf";
	dieAnimInfo.animationName = "playerDieAnimation";
	dieAnimInfo.animationFilename = "playerDie.gltf";
	modelManager->LoadAnimation(dieAnimInfo);

	// スケルトンアニメーションモデルとして作成（初期は待機アニメーション）
	auto playerModel = modelManager->CreateSkeletonModel(
		"Assets/AppAssets/Player/player.gltf",
		"playerIdleAnimation",
		true
	);

	// テクスチャを読み込む
	auto playerTexture = CoreEngine::TextureManager::GetInstance().Load("Assets/AppAssets/Player/player.png");

	// プレイヤーオブジェクトの生成と初期化
	player_ = CreateObject<PlayerObject>();
	player_->Initialize(std::move(playerModel), playerTexture);
	player_->SetActive(true);

	// オープニング演出中は移動不可
	player_->SetCutscenePlaying(true);

	// プレイヤーのサウンドリソースを設定
	auto soundManager = engine_->GetComponent<CoreEngine::SoundManager>();
	if (soundManager) {
		auto bulletShotSound = soundManager->CreateSoundResource("Audio/SE/Player/bulletShot.mp3");
		auto playerDieSound = soundManager->CreateSoundResource("Audio/SE/Player/playerDie.mp3");
		auto hitSound = soundManager->CreateSoundResource("Audio/SE/Player/hit.mp3");

		// 弾発射SEの音量を0.2に設定
		if (bulletShotSound && bulletShotSound->IsValid()) {
			bulletShotSound->SetVolume(0.2f);
			hitSound->SetVolume(0.4f);
		}

		player_->SetSoundResources(std::move(bulletShotSound), std::move(playerDieSound), std::move(hitSound));
	}
}

void GameScene::InitializeReticle() {
	// レティクルの初期化
	reticle_ = CreateObject<ReticleObject>();
	reticle_->Initialize("Assets/Texture/rethicle.png");
	reticle_->SetMoveSpeed(400.0f);
	reticle_->SetMoveMargin(30.0f, 30.0f);
	reticle_->SetActive(true);
}

void GameScene::InitializeBoss() {
	// ModelManagerを取得
	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) {
		return;
	}

	// ===== ボスのアニメーションを読み込む =====

	// 歩くアニメーション（ベースモデル、デフォルト）
	AnimationLoadInfo bossWalkAnimInfo;
	bossWalkAnimInfo.directory = "Assets/AppAssets/Boss";
	bossWalkAnimInfo.modelFilename = "bossWalk.gltf";
	bossWalkAnimInfo.animationName = "bossWalkAnimation";
	bossWalkAnimInfo.animationFilename = "bossWalk.gltf";
	modelManager->LoadAnimation(bossWalkAnimInfo);

	// 待機アニメーション
	AnimationLoadInfo bossIdleAnimInfo;
	bossIdleAnimInfo.directory = "Assets/AppAssets/Boss";
	bossIdleAnimInfo.modelFilename = "bossWalk.gltf";
	bossIdleAnimInfo.animationName = "bossIdleAnimation";
	bossIdleAnimInfo.animationFilename = "bossIdle.gltf";
	modelManager->LoadAnimation(bossIdleAnimInfo);

	// ジャンプ攻撃アニメーション
	AnimationLoadInfo bossJumpAttackAnimInfo;
	bossJumpAttackAnimInfo.directory = "Assets/AppAssets/Boss";
	bossJumpAttackAnimInfo.modelFilename = "bossWalk.gltf";
	bossJumpAttackAnimInfo.animationName = "bossJumpAttackAnimation";
	bossJumpAttackAnimInfo.animationFilename = "bossJumpAttack.gltf";
	modelManager->LoadAnimation(bossJumpAttackAnimInfo);

	// パンチ攻撃アニメーション
	AnimationLoadInfo bossPunchAnimInfo;
	bossPunchAnimInfo.directory = "Assets/AppAssets/Boss";
	bossPunchAnimInfo.modelFilename = "bossWalk.gltf";
	bossPunchAnimInfo.animationName = "bossPunchAnimation";
	bossPunchAnimInfo.animationFilename = "bossPunch.gltf";
	modelManager->LoadAnimation(bossPunchAnimInfo);

	// 突進攻撃アニメーション
	AnimationLoadInfo bossDashAnimInfo;
	bossDashAnimInfo.directory = "Assets/AppAssets/Boss";
	bossDashAnimInfo.modelFilename = "bossWalk.gltf";
	bossDashAnimInfo.animationName = "bossDashAttackAnimation";
	bossDashAnimInfo.animationFilename = "bossDushAttack.gltf";
	modelManager->LoadAnimation(bossDashAnimInfo);

	// ジャンプアニメーション
	AnimationLoadInfo bossJumpAnimInfo;
	bossJumpAnimInfo.directory = "Assets/AppAssets/Boss";
	bossJumpAnimInfo.modelFilename = "bossWalk.gltf";
	bossJumpAnimInfo.animationName = "bossJumpAnimation";
	bossJumpAnimInfo.animationFilename = "bossJump.gltf";
	modelManager->LoadAnimation(bossJumpAnimInfo);

	// 撃破アニメーション
	AnimationLoadInfo bossDestroyAnimInfo;
	bossDestroyAnimInfo.directory = "Assets/AppAssets/Boss";
	bossDestroyAnimInfo.modelFilename = "bossWalk.gltf";
	bossDestroyAnimInfo.animationName = "bossDestroyAnimation";
	bossDestroyAnimInfo.animationFilename = "bossDestroy.gltf";
	modelManager->LoadAnimation(bossDestroyAnimInfo);

	// ===== ボスモデルパーティクル用のモデルを読み込む =====
	auto bossModelParticle = modelManager->CreateStaticModel("Assets/AppAssets/Boss/BossModelParticle/bossModelParticle.obj");

	// ===== ボクセルモデルを事前にロード（パーティクル用） =====
	auto voxelModel = modelManager->CreateStaticModel("Assets/AppAssets/Voxel/Voxel.obj");

	// スケルトンアニメーションモデルとして作成（初期は待機アニメーション）
	auto bossModel = modelManager->CreateSkeletonModel(
		"Assets/AppAssets/Boss/bossWalk.gltf",
		"bossIdleAnimation",
		true
	);

	// テクスチャを読み込む
	auto bossTexture = CoreEngine::TextureManager::GetInstance().Load("Assets/AppAssets/Boss/boss.png");

	// ボスオブジェクトの生成と初期化
	boss_ = CreateObject<BossObject>();
	boss_->Initialize(std::move(bossModel), bossTexture);
	boss_->SetActive(true);

	// ボスのサウンドリソースを設定
	auto soundManager = engine_->GetComponent<CoreEngine::SoundManager>();
	if (soundManager) {
		auto bossDieSound = soundManager->CreateSoundResource("Audio/SE/Boss/bossDie.mp3");
		auto bossJumpAttackSound = soundManager->CreateSoundResource("Audio/SE/Boss/JumpAttack.mp3");
		auto bossPunchSound = soundManager->CreateSoundResource("Audio/SE/Boss/bossPanchi.mp3");
		boss_->SetSoundResources(std::move(bossDieSound), std::move(bossJumpAttackSound), std::move(bossPunchSound));
	}

	// オープニング演出中はボスのAIを無効化
	boss_->SetAIEnabled(false);

	// ヒットボックス生成コールバックを設定
	boss_->SetHitboxSpawnCallback([this](std::unique_ptr<JumpAttackHitbox> hitbox) {
		SpawnJumpAttackHitbox(std::move(hitbox));
		});

	// 弾幕弾生成コールバックを設定
	boss_->SetBarrageBulletSpawnCallback([this](const Vector3& position, const Vector3& direction) {
		SpawnBossBullet(position, direction);
		});

	// 影非表示コールバックを設定
	boss_->SetHideShadowCallback([this]() {
		if (bossShadow_) {
			bossShadow_->SetActive(false);
		}
		});

	// AIを初期化（プレイヤー参照を渡す）
	if (player_) {
		boss_->InitializeAI(player_);
	}

	// ボスのコライダーを登録
	if (boss_->GetCollider() && gamePlayManager_) {
		gamePlayManager_->GetCollisionManager()->RegisterCollider(static_cast<Collider*>(boss_->GetCollider()));
	}
}

void GameScene::InitializeDebugUI() {
	// ボスAIデバッグUIの初期化
	bossAIDebugUI_ = std::make_unique<BossAIDebugUI>();
	if (boss_) {
		bossAIDebugUI_->Initialize(boss_);
	}
}

void GameScene::InitializeCameraController() {
	// カメラコントローラーを作成
	cameraController_ = std::make_unique<GameCameraController>();

	// カメラを取得
	CoreEngine::ICamera* camera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
	if (!camera) return;

	// カメラコントローラーを初期化
	cameraController_->Initialize(engine_, camera, player_, boss_);

	// プレイヤーにカメラコントローラーを設定
	if (player_) {
		player_->SetCameraController(cameraController_.get());
		
		// プレイヤーにレティクルを設定
		if (reticle_) {
			player_->SetReticle(reticle_);
		}
		
		// プレイヤーにカメラを設定
		player_->SetCamera(camera);
	}

	// オープニング終了時のコールバックを設定
	cameraController_->SetOnOpeningFinishedCallback([this]() {
		OnOpeningFinished();
		});

	// BGMの読み込みと再生
	auto soundManager = engine_->GetComponent<CoreEngine::SoundManager>();
	if (soundManager) {
		gameBGM_ = soundManager->CreateSoundResource("Audio/BGM/gameScene.mp3");

		if (gameBGM_ && gameBGM_->IsValid()) {
			gameBGM_->SetVolume(0.2f);  // 基本音量を設定
			gameBGM_->Play(true);  // ループ再生

			// BGMをシーンに登録してトランジション時の自動フェードを有効化
			RegisterSceneBGM(&gameBGM_);
		}
	}
}

void GameScene::InitializeSkydome() {
	// 天球オブジェクトの生成と初期化
	auto skydome = CreateObject<SkydomeObject>();
	skydome->Initialize();
	skydome->SetActive(true);
}

void GameScene::InitializeWall() {
	// アリーナの大きさ設定（壁の配置距離）
	constexpr float ARENA_SIZE = 40.0f;  // アリーナの半径（30 → 40に拡大）
	constexpr float WALL_HEIGHT = 2.5f;  // 壁の高さ（Y座標）
	constexpr float PI = std::numbers::pi_v<float>;

	// 壁のスケール（変更しない）
	Vector3 wallScale = { 1.0f, 1.0f, 1.0f };

	// 北側の壁（+Z方向）
	auto wallNorth = CreateObject<WallObject>();
	wallNorth->Initialize(
		Vector3{ 0.0f, WALL_HEIGHT, ARENA_SIZE },  // 位置
		0.0f,  // 回転（Y軸）
		wallScale  // スケール
	);
	wallNorth->SetActive(true);

	// 南側の壁（-Z方向）
	auto wallSouth = CreateObject<WallObject>();
	wallSouth->Initialize(
		Vector3{ 0.0f, WALL_HEIGHT, -ARENA_SIZE },  // 位置
		PI,  // 180度回転
		wallScale  // スケール
	);
	wallSouth->SetActive(true);

	// 東側の壁（+X方向）
	auto wallEast = CreateObject<WallObject>();
	wallEast->Initialize(
		Vector3{ ARENA_SIZE, WALL_HEIGHT, 0.0f },  // 位置
		PI * 0.5f,  // 90度回転
		wallScale  // スケール
	);
	wallEast->SetActive(true);

	// 西側の壁（-X方向）
	auto wallWest = CreateObject<WallObject>();
	wallWest->Initialize(
		Vector3{ -ARENA_SIZE, WALL_HEIGHT, 0.0f },  // 位置
		-PI * 0.5f,  // -90度回転
		wallScale  // スケール
	);
	wallWest->SetActive(true);
}

void GameScene::InitializeFloor() {
	// 床オブジェクトの生成と初期化
	auto floor = CreateObject<FloorObject>();
	floor->Initialize();
	floor->SetActive(true);
}

void GameScene::InitializeShadows() {
	// プレイヤー用の丸影オブジェクトの生成と初期化
	playerShadow_ = CreateObject<CircleShadowObject>();
	playerShadow_->Initialize(2.0f);  // 直径2.0fの影
	playerShadow_->SetShadowY(-0.9f);  // 影のY座標
	playerShadow_->SetShadowAlpha(0.5f); // 影の濃さ
	playerShadow_->SetActive(true);

	// ボス用の丸影オブジェクトの生成と初期化
	bossShadow_ = CreateObject<CircleShadowObject>();
	bossShadow_->Initialize(5.0f);  // 直径4.0f（ボスは大きめ）
	bossShadow_->SetShadowY(-1.0f);  // 影のY座標
	bossShadow_->SetShadowAlpha(0.6f); // ボスの影は少し濃いめ
	bossShadow_->SetActive(true);
}

void GameScene::InitializePostEffects() {
	// PostEffectManagerを取得
	auto postEffectManager = engine_->GetComponent<CoreEngine::PostEffectManager>();
	if (!postEffectManager) {
		return;
	}

	// ===== Vignette（ヴィネット）エフェクトを有効化 =====
	// 画面の端を暗くすることで、ダンジョンの閉塞感と緊張感を演出
	postEffectManager->SetEffectEnabled(PostEffectNames::Vignette, true);
	
	auto* vignette = postEffectManager->GetEffect<Vignette>(PostEffectNames::Vignette);
	if (vignette) {
		Vignette::VignetteParams vignetteParams;
		vignetteParams.intensity = 1.2f;    // 強度を高めに設定（暗い雰囲気）
		vignetteParams.smoothness = 0.6f;   // 滑らかさ（自然なグラデーション）
		vignetteParams.size = 12.0f;        // サイズ（エッジからの距離）
		vignette->SetParams(vignetteParams);
	}

	// カラーグレーディングは無効化
	postEffectManager->SetEffectEnabled(PostEffectNames::ColorGrading, false);
}

void GameScene::OnOpeningFinished() {
	// オープニング演出が終了
	isOpeningPlaying_ = false;

	// プレイヤーの演出フラグを解除（移動可能に）
	if (player_) {
		player_->SetCutscenePlaying(false);
	}

	// UI表示演出を開始
	if (uiManager_) {
		uiManager_->StartUIShowAnimation();
	}

	// ボスのAIを有効化して行動を開始
	if (boss_) {
		boss_->SetAIEnabled(true);
		// 待機アニメーションから歩くアニメーションに切り替え
		boss_->PlayWalkAnimation();
	}
}

void GameScene::OnUpdate()
{
	// カメラコントローラーの更新
	if (cameraController_) {
		cameraController_->Update();
		
		// オープニング演出中は全UIを非表示
		if (uiManager_) {
			if (cameraController_->IsOpeningPlaying()) {
				uiManager_->SetUIVisible(false);
			} else {
				uiManager_->SetUIVisible(true);
			}
		}
	}

	// 影の位置を更新（プレイヤーとボスの足元に追従）
	if (playerShadow_ && player_) {
		playerShadow_->SetTargetPosition(player_->GetWorldPosition());
		playerShadow_->Update();
	}

	if (bossShadow_ && boss_) {
		// ボスがジャンプ攻撃中の場合、開始位置から着地位置へ影を補間移動
		if (boss_->IsJumpAttacking()) {
			float progress = boss_->GetJumpAttackProgress();
			
			// 着地タイミング（80%）以降は着地位置に固定
			constexpr float landingTiming = 0.8f;
			
			if (progress >= landingTiming) {
				// 着地タイミング以降は着地位置に固定
				bossShadow_->SetTargetPosition(boss_->GetJumpAttackLandingPosition());
			} else {
				// 着地タイミングまでは補間移動（進行度を0〜1にリマップ)
				float normalizedProgress = progress / landingTiming;
				
				// ジャンプ開始時に保存された位置を使用
				Vector3 startPos = boss_->GetTransform().translate;  // ジャンプ開始位置
				Vector3 landingPos = boss_->GetJumpAttackLandingPosition();
				
				// 開始位置から着地位置へ線形補間（XZ平面のみ）
				Vector3 shadowPos = {
					startPos.x + (landingPos.x - startPos.x) * normalizedProgress,
					0.0f,  // Y座標は後で設定される
					startPos.z + (landingPos.z - startPos.z) * normalizedProgress
				};
				
				bossShadow_->SetTargetPosition(shadowPos);
			}
		} else {
			// 通常時はボスの位置に追従
			bossShadow_->SetTargetPosition(boss_->GetTransform().GetWorldPosition());
		}
		bossShadow_->Update();
	}

#ifdef _DEBUG
	// デバッグUIの更新
	UpdateDebugUI();
#endif

	// UIマネージャーの更新
	if (uiManager_) {
		uiManager_->Update();
		
		// RB入力状態を更新（プレイヤーの射撃入力を取得）
		auto inputManager = engine_->GetComponent<InputManager>();
		if (inputManager) {
			bool isRBPressed = false;
			
			// キーボード：スペースキーが押されているかチェック
			auto keyboard = inputManager->GetKeyboard();
			if (keyboard && keyboard->IsKeyPressed(DIK_SPACE)) {
				isRBPressed = true;
			}
			
			// ゲームパッド：RTトリガーが押されているかチェック（閾値0.5以上）
			auto gamepad = inputManager->GetGamepad();
			if (gamepad && gamepad->IsConnected()) {
				constexpr float TRIGGER_THRESHOLD = 0.5f;
				if (gamepad->GetRightTrigger() >= TRIGGER_THRESHOLD) {
					isRBPressed = true;
				}
			}
			
			// RB UIの表示を更新
			uiManager_->UpdateRBInput(isRBPressed);
		}
	}

	// ボスが撃破された場合の処理
	if (boss_ && boss_->IsDestroyed() && cameraController_) {
		// ボス撃破演出がまだ開始していない場合に開始
		if (cameraController_->GetCurrentState() == GameCameraController::CameraState::FollowPlayer) {
			cameraController_->StartBossDefeatSequence();

			// プレイヤーの移動を停止（演出中フラグを設定）
			if (player_) {
				player_->SetCutscenePlaying(true);
			}

			// 勝利演出を開始
			if (uiManager_) {
				uiManager_->StartWinAnimation();
			}

			// 撃破演出終了時のコールバックを設定
			cameraController_->SetOnBossDefeatFinishedCallback([this]() {
				// カメラ演出が終了したらフラグを立てる
				isBossDefeatCameraFinished_ = true;
				});
		}

		// カメラ演出が終了し、かつパーティクルも終了したらシーン遷移
		if (isBossDefeatCameraFinished_ && gamePlayManager_ && gamePlayManager_->IsBossDestroyParticleFinished()) {
			// タイトルシーンへ遷移
			if (sceneManager_) {
				sceneManager_->ChangeScene("TitleScene");
			}
		}

		// 撃破演出中はゲームロジックをスキップ
		return;
	}

	// プレイヤーが死亡した場合の処理
	if (player_ && player_->IsDead() && cameraController_) {
		// プレイヤー死亡演出がまだ開始していない場合に開始
		if (cameraController_->GetCurrentState() == GameCameraController::CameraState::FollowPlayer) {
			cameraController_->StartPlayerDeathSequence();

			// プレイヤーの移動を停止（演出中フラグを設定）
			if (player_) {
				player_->SetCutscenePlaying(true);
			}

			// 敗北演出を開始
			if (uiManager_) {
				uiManager_->StartLoseAnimation();
			}

			// 死亡演出終了時のコールバックを設定
			cameraController_->SetOnPlayerDeathFinishedCallback([this]() {
				// 演出終了後、タイトルシーンへ遷移
				if (sceneManager_) {
					sceneManager_->ChangeScene("TitleScene");
				}
				});
		}

		// 死亡演出中はゲームロジックをスキップ
		return;
	}

#ifdef _DEBUG
	// コライダーのデバッグ描画の更新
	UpdateColliderDebug();
#endif

	// KeyboardInput を直接取得
	auto keyboard = engine_->GetComponent<CoreEngine::KeyboardInput>();
	if (!keyboard) {
		return;
	}

	// Rキーでリザルトシーンへ遷移
	if (keyboard->IsKeyTriggered(DIK_R)) {
		if (sceneManager_) {
			sceneManager_->ChangeScene("ResultScene");
		}
	}

	// ゲームプレイマネージャーの更新
	if (gamePlayManager_) {
		gamePlayManager_->Update(isOpeningPlaying_);
	}
}

void GameScene::Draw()
{
	// 基底クラスの描画（この中でLineDrawableも自動的に描画される）
	BaseScene::Draw();
}

#ifdef _DEBUG
void GameScene::UpdateDebugUI() {
	if (bossAIDebugUI_) {
		bossAIDebugUI_->Update();
	}
}

void GameScene::UpdateColliderDebug() {
	if (!lineRenderer_ || !colliderDebugLines_ || !gamePlayManager_) return;

	// カメラを取得
	CoreEngine::ICamera* camera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
	if (!camera) return;

	// 前フレームのラインをクリア
	colliderDebugLines_->Clear();

	// ラインのリストを作成
	std::vector<Line> debugLines;

	// プレイヤーのコライダーを描画（緑色）
	if (player_ && player_->GetCollider()) {
		Vector3 center = player_->GetCollider()->GetPosition();
		Vector3 size = player_->GetCollider()->GetSize();
		auto lines = LineManager::GenerateBoxLines(center, size, Vector3{ 0.0f, 1.0f, 0.0f }, 1.0f);
		debugLines.insert(debugLines.end(), lines.begin(), lines.end());
	}

	// ボスのコライダーを描画（赤色）	
	if (boss_ && boss_->GetCollider()) {
		Vector3 center = boss_->GetCollider()->GetPosition();
		Vector3 size = boss_->GetCollider()->GetSize();
		auto lines = LineManager::GenerateBoxLines(center, size, Vector3{ 1.0f, 0.0f, 0.0f }, 1.0f);
		debugLines.insert(debugLines.end(), lines.begin(), lines.end());
	}

	// 衝突マネージャーから全てのコライダーを取得してデバッグ描画
	auto collisionManager = gamePlayManager_->GetCollisionManager();
	if (collisionManager) {
		const auto& colliders = collisionManager->GetAllColliders();

		for (auto* collider : colliders) {
			if (!collider) continue;

			// 球コライダーの場合
			auto sphereCollider = dynamic_cast<SphereCollider*>(collider);
			if (sphereCollider) {
				Vector3 center = sphereCollider->GetPosition();
				float radius = sphereCollider->GetRadius();

				// レイヤーに応じて色を変える
				Vector3 color = { 1.0f, 1.0f, 0.0f };  // デフォルトは黄色
				if (collider->GetLayer() == CollisionLayer::PlayerBullet) {
					color = { 1.0f, 1.0f, 0.0f };  // 黄色
				}

				auto lines = LineManager::GenerateSphereLines(center, radius, color, 1.0f, 16);
				debugLines.insert(debugLines.end(), lines.begin(), lines.end());
			}
			else {
				auto aabbCollider = dynamic_cast<CoreEngine::AABBCollider*>(collider);
				if (aabbCollider) {
					Vector3 center = aabbCollider->GetPosition();
					Vector3 size = aabbCollider->GetSize();

					// レイヤーに応じて色を変える
					Vector3 color = { 1.0f, 0.5f, 0.0f };  // デフォルトはオレンジ
					if (collider->GetLayer() == CollisionLayer::BossAttack) {
						color = { 1.0f, 0.5f, 0.0f };  // オレンジ
					}

					auto lines = LineManager::GenerateBoxLines(center, size, color, 1.0f);
					debugLines.insert(debugLines.end(), lines.begin(), lines.end());
				}
			}
		}
	}

	// すべてのラインをLineDrawableに追加
	if (!debugLines.empty()) {
		colliderDebugLines_->AddLines(debugLines);
	}
}
#endif

void GameScene::Finalize()
{
	// BGMを停止
	if (gameBGM_ && gameBGM_->IsValid()) {
		gameBGM_->Stop();
	}

	// 基底クラスの終了処理
	BaseScene::Finalize();
}

void GameScene::SpawnBullet() {
	if (!player_ || !gamePlayManager_) return;

	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) return;

	// 弾モデルを作成（OBJファイル - 静的モデル）
	auto bulletModel = modelManager->CreateStaticModel("Assets/AppAssets/Player/PlayerBullet/playerBullet.obj");

	// テクスチャを読み込む
	auto bulletTexture = CoreEngine::TextureManager::GetInstance().Load("Assets/AppAssets/Player/PlayerBullet/playerBullet.png");

	// 発射位置を取得
	Vector3 spawnPosition = player_->GetBulletSpawnPosition();

	// プレイヤーの前方向を取得
	Vector3 direction = player_->GetBulletDirection();

	// 弾オブジェクトの生成と初期化
	auto bullet = CreateObject<BulletObject>();
	bullet->Initialize(std::move(bulletModel), bulletTexture, spawnPosition, direction);
	bullet->SetActive(true);

	// GamePlayManagerに弾を追加
	gamePlayManager_->AddBullet(bullet);
}

void GameScene::SpawnJumpAttackHitbox(std::unique_ptr<JumpAttackHitbox> hitbox) {
	if (!gamePlayManager_) return;

	JumpAttackHitbox* hitboxPtr = hitbox.get();

	// GamePlayManagerにヒットボックスを追加
	gamePlayManager_->AddJumpAttackHitbox(hitboxPtr);

	// GameObjectManagerに登録
	gameObjectManager_.AddObject(std::move(hitbox));
}

void GameScene::SpawnBossBullet(const Vector3& position, const Vector3& direction) {
	if (!gamePlayManager_) return;

	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) return;

	// ボス弾モデルを作成（OBJファイル - 静的モデル）
	auto bossBulletModel = modelManager->CreateStaticModel("Assets/AppAssets/Boss/BossBullet/bossBullet.obj");

	// テクスチャを読み込む
	auto bossBulletTexture = CoreEngine::TextureManager::GetInstance().Load("Assets/AppAssets/Boss/BossBullet/bossBullet.png");

	// ボス弾オブジェクトの生成と初期化
	auto bossBullet = CreateObject<BossBulletObject>();
	bossBullet->Initialize(std::move(bossBulletModel), bossBulletTexture, position, direction);
	bossBullet->SetActive(true);

	// GamePlayManagerにボス弾を追加
	gamePlayManager_->AddBossBullet(bossBullet);
}
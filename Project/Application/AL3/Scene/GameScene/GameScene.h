#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Application/AL3/Manager/GamePlayManager.h"
#include "Application/AL3/Manager/GameUIManager.h"
#include <list>
#include <memory>

// デバッグUI（完全なインクルードが必要）
#include "Application/AL3/Debug/BossAIDebugUI.h"
#include "Application/AL3/Camera/GameCameraController.h"

// 前方宣言
class PlayerObject;
class BulletObject;
class ReticleObject;
class BossObject;
class JumpAttackHitbox;
class SkydomeObject;
class WallObject;
class FloorObject;
class CircleShadowObject;

namespace CoreEngine
{
class LineRendererPipeline;
class LineDrawable;
class ParticleSystem;
class SpriteObject;
}

class GameCameraController;

/// @brief ゲームシーンクラス
class GameScene : public CoreEngine::BaseScene {
public:
	/// @brief 初期化
	void Initialize(CoreEngine::EngineSystem* engine) override;

	/// @brief 描画処理
	void Draw() override;

	/// @brief 解放
	void Finalize() override;

protected:
	/// @brief 更新
	void OnUpdate() override;

private:
	/// @brief プレイヤーの初期化
	void InitializePlayer();

	/// @brief レティクルの初期化
	void InitializeReticle();

	/// @brief ボスの初期化
	void InitializeBoss();

	/// @brief デバッグUIの初期化
	void InitializeDebugUI();

	/// @brief カメラコントローラーの初期化
	void InitializeCameraController();

	/// @brief 天球の初期化
	void InitializeSkydome();

	/// @brief 壁の初期化
	void InitializeWall();

	/// @brief 床の初期化
	void InitializeFloor();

	/// @brief 丸影の初期化
	void InitializeShadows();

	/// @brief ポストエフェクトの初期化
	void InitializePostEffects();

	/// @brief オープニング演出終了時のコールバック
	void OnOpeningFinished();

#ifdef _DEBUG
	/// @brief デバッグUIの更新
	void UpdateDebugUI();

	/// @brief コライダーのデバッグ描画の更新
	void UpdateColliderDebug();
#endif

	/// @brief 弾を生成する
	void SpawnBullet();

	/// @brief ボス弾を生成する
	/// @param position 発射位置
	/// @param direction 発射方向
	void SpawnBossBullet(const CoreEngine::Vector3& position, const CoreEngine::Vector3& direction);

	/// @brief ジャンプ攻撃ヒットボックスを生成
	void SpawnJumpAttackHitbox(std::unique_ptr<JumpAttackHitbox> hitbox);

	// プレイヤーへのポインタ
	PlayerObject* player_ = nullptr;

	// レティクル（照準）
	ReticleObject* reticle_ = nullptr;

	// ボス
	BossObject* boss_ = nullptr;

	// ゲームプレイマネージャー
	std::unique_ptr<GamePlayManager> gamePlayManager_;

	// UIマネージャー
	std::unique_ptr<GameUIManager> uiManager_;

	// デバッグUI
	std::unique_ptr<BossAIDebugUI> bossAIDebugUI_;

	// カメラコントローラー
	std::unique_ptr<GameCameraController> cameraController_;

	// オープニング演出中フラグ
	bool isOpeningPlaying_ = true;

	// ボス撃破演出のカメラ終了フラグ
	bool isBossDefeatCameraFinished_ = false;

	// サウンドリソース
	std::unique_ptr<CoreEngine::SoundManager::SoundResource> gameBGM_;  // ゲームBGM

	// 丸影オブジェクト
	CircleShadowObject* playerShadow_ = nullptr;
	CircleShadowObject* bossShadow_ = nullptr;

#ifdef _DEBUG
	// デバッグ描画用
	CoreEngine::LineRendererPipeline* lineRenderer_ = nullptr;
	CoreEngine::LineDrawable* colliderDebugLines_ = nullptr;
#endif
};

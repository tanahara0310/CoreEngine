#pragma once

#include <string>
#include <functional>

// 前方宣言
namespace CoreEngine {
	class EngineSystem;
	class SpriteObject;
}
class PlayerObject;
class BossObject;
class ReticleObject;

/// @brief ゲーム内UIを管理するクラス
class GameUIManager {
public:
	/// @brief 初期化
	/// @param engine エンジンシステム
	/// @param player プレイヤー
	/// @param boss ボス
	/// @param reticle レティクル
	void Initialize(
		CoreEngine::EngineSystem* engine,
		PlayerObject* player,
		BossObject* boss,
		ReticleObject* reticle
	);

	/// @brief ボス名画像の初期化
	/// @param createSpriteCallback スプライト生成コールバック
	void InitializeBossName(std::function<CoreEngine::SpriteObject*(const std::string&, const std::string&)> createSpriteCallback);

	/// @brief ボスHPバーの初期化
	/// @param createSpriteCallback スプライト生成コールバック
	void InitializeBossHPBar(std::function<CoreEngine::SpriteObject*(const std::string&, const std::string&)> createSpriteCallback);

	/// @brief 操作方法UIの初期化
	/// @param createSpriteCallback スプライト生成コールバック
	void InitializeControlUI(std::function<CoreEngine::SpriteObject*(const std::string&, const std::string&)> createSpriteCallback);

	/// @brief プレイヤーHPバーの初期化
	/// @param createSpriteCallback スプライト生成コールバック
	void InitializePlayerHPBar(std::function<CoreEngine::SpriteObject*(const std::string&, const std::string&)> createSpriteCallback);

	/// @brief リザルトスプライトの初期化
	/// @param createSpriteCallback スプライト生成コールバック
	void InitializeResultSprites(std::function<CoreEngine::SpriteObject*(const std::string&, const std::string&)> createSpriteCallback);

	/// @brief 全UIの表示/非表示を設定
	/// @param visible 表示するか
	void SetUIVisible(bool visible);

	/// @brief UI表示演出を開始
	void StartUIShowAnimation();

	/// @brief RB入力状態を更新
	/// @param isPressed RBが押されているか
	void UpdateRBInput(bool isPressed);

	/// @brief 勝利演出を開始
	void StartWinAnimation();

	/// @brief 敗北演出を開始
	void StartLoseAnimation();

	/// @brief 更新
	void Update();

private:
	/// @brief ボスHPバーの更新
	/// @param deltaTime デルタタイム
	void UpdateBossHPBar(float deltaTime);

	/// @brief プレイヤーHPバーの更新
	/// @param deltaTime デルタタイム
	void UpdatePlayerHPBar(float deltaTime);

	/// @brief リザルト演出の更新
	/// @param deltaTime デルタタイム
	void UpdateResultAnimation(float deltaTime);

	CoreEngine::EngineSystem* engine_ = nullptr;
	PlayerObject* player_ = nullptr;
	BossObject* boss_ = nullptr;
	ReticleObject* reticle_ = nullptr;

	// ボス名画像
	CoreEngine::SpriteObject* bossNameSprite_ = nullptr;

	// ボスHPバー（背景、遅延、前景）
	CoreEngine::SpriteObject* bossHPBarBackground_ = nullptr;  // HP背景（グレー）
	CoreEngine::SpriteObject* bossHPBarDelay_ = nullptr;       // HP遅延バー（赤・ダメージ表示用）
	CoreEngine::SpriteObject* bossHPBarForeground_ = nullptr;  // HP前景（緑）

	// プレイヤーHPバー（背景、遅延、前景）
	CoreEngine::SpriteObject* playerHPBarBackground_ = nullptr;  // HP背景（黒）
	CoreEngine::SpriteObject* playerHPBarDelay_ = nullptr;       // HP遅延バー（赤・ダメージ表示用）
	CoreEngine::SpriteObject* playerHPBarForeground_ = nullptr;  // HP前景（緑）
	CoreEngine::SpriteObject* playerHPText_ = nullptr;           // HPテキストスプライト

	// 操作方法UI
	CoreEngine::SpriteObject* playerMoveSprite_ = nullptr;     // プレイヤー移動UI（左下）
	CoreEngine::SpriteObject* reticleMoveSprite_ = nullptr;    // レティクル移動UI（右下）
	CoreEngine::SpriteObject* bulletShotSprite_ = nullptr;     // 弾発射UI（右下、レティクル移動の上）

	// Xboxコントローラー操作UI
	CoreEngine::SpriteObject* xboxRbSprite_ = nullptr;                 // RBボタンUI
	CoreEngine::SpriteObject* xboxStickLHorizontalSprite_ = nullptr;   // 左スティック横移動UI
	CoreEngine::SpriteObject* xboxStickLVerticalSprite_ = nullptr;     // 左スティック縦移動UI
	CoreEngine::SpriteObject* xboxStickRHorizontalSprite_ = nullptr;   // 右スティック横移動UI
	CoreEngine::SpriteObject* xboxStickRVerticalSprite_ = nullptr;     // 右スティック縦移動UI

	// リザルトスプライト
	CoreEngine::SpriteObject* winSprite_ = nullptr;    // 勝利スプライト
	CoreEngine::SpriteObject* loseSprite_ = nullptr;   // 敗北スプライト

	// HPバーのサイズと位置の定数
	static constexpr float HP_BAR_WIDTH = 600.0f;   // HPバー最大幅（800 → 600に縮小）
	static constexpr float HP_BAR_HEIGHT = 16.0f;   // HPバーの高さ（20 → 16に縮小）
	static constexpr float HP_BAR_Y = 280.0f;       // ボスHPバーのY座標（ボス名の下）
	static constexpr float PLAYER_HP_BAR_WIDTH = 400.0f;  // プレイヤーHPバー幅（ボスより短く)
	static constexpr float PLAYER_HP_BAR_Y = -310.0f;  // プレイヤーHPバーのY座標（画面中央下、より下に配置）

	// 遅延HPバー用の変数（ボス用）
	float delayedHPRatio_ = 1.0f;                   // 遅延HPの割合（0.0〜1.0）
	static constexpr float HP_DELAY_SPEED_FAST = 0.08f;  // HP減少の初期速度（速め）
	static constexpr float HP_DELAY_SPEED_SLOW = 0.03f;  // HP減少の後半速度（遅め）
	static constexpr float HP_DELAY_THRESHOLD = 0.05f;   // 速度切り替えの閾値（差が5%以下で遅くなる）

	// 遅延HPバー用の変数（プレイヤー用）
	float playerDelayedHPRatio_ = 1.0f;             // プレイヤー遅延HPの割合（0.0〜1.0）

	// リザルト演出用の変数
	bool isResultAnimating_ = false;                // リザルト演出中か
	float resultAnimationTimer_ = 0.0f;             // リザルト演出タイマー
	static constexpr float RESULT_ANIMATION_DURATION = 1.5f;  // リザルト演出時間（秒）
	static constexpr float RESULT_START_Y = 100.0f;  // リザルトスプライトの初期Y座標（画面中央やや上）
	static constexpr float RESULT_TARGET_Y = 0.0f;   // リザルトスプライトの目標Y座標（画面中央）

	// UI表示演出用の変数
	bool isUIAnimating_ = false;                    // UI表示演出中か
	float uiAnimationTimer_ = 0.0f;                 // 演出タイマー
	static constexpr float UI_ANIMATION_DURATION = 1.5f;  // 演出時間（秒）1.0 → 1.5に延長
	
	// HPバー演出用の初期位置と回転
	float hpBarInitialY_ = 0.0f;                    // HPバーの初期Y座標（画面中央）
	float hpBarInitialRotation_ = 0.0f;             // HPバーの初期回転（ラジアン）
	float hpBarAnimationProgress_ = 0.0f;           // HPバー演出の進行度（0.0〜1.0）
	
	// HP増加演出用の変数
	static constexpr float HP_BAR_ROTATION_DURATION = 0.5f;  // 回転演出の時間（秒）
	static constexpr float HP_FILL_DURATION = 1.0f;          // HP増加演出の時間（秒）
};

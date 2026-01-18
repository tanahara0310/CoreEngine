#pragma once

#include "MathCore.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include "Engine/Math/Easing/EasingUtil.h"
#include <memory>
#include <functional>

// 前方宣言
namespace CoreEngine {
	class ICamera;
	class EngineSystem;
}
class PlayerObject;
class BossObject;

/// @brief ゲームシーン用カメラコントローラー
/// @details 演出とゲームプレイ中のカメラ制御を管理
class GameCameraController {
public:
	/// @brief カメラの状態
	enum class CameraState {
		OpeningBossIntro,    // オープニング：ボス登場演出（ラジアルブラー付き）
		OpeningBossFocus,    // オープニング：ボス登場演出（回り込み）
		OpeningToPlayer,     // オープニング：プレイヤーへ移動
		OpeningFadeOut,      // オープニング：フェードアウト
		OpeningFadeIn,       // オープニング：フェードイン
		FollowPlayer,        // ゲームプレイ：プレイヤーを追従
		BossDefeatFocus,     // ボス撃破：ボスにフォーカス
		PlayerDeathFocus     // プレイヤー死亡：プレイヤーにフォーカス
	};

	/// @brief 初期化
	/// @param engine エンジンシステム
	/// @param camera 制御するカメラ
	/// @param player プレイヤーオブジェクト
	/// @param boss ボスオブジェクト
	void Initialize(CoreEngine::EngineSystem* engine, CoreEngine::ICamera* camera, PlayerObject* player, BossObject* boss);

	/// @brief 更新処理
	void Update();

	/// @brief オープニング演出を開始
	void StartOpeningSequence();

	/// @brief ボス撃破演出を開始
	void StartBossDefeatSequence();

	/// @brief プレイヤー死亡演出を開始
	void StartPlayerDeathSequence();

	/// @brief オープニング演出が終了しているか
	/// @return 終了している場合true
	bool IsOpeningFinished() const { return currentState_ == CameraState::FollowPlayer; }

	/// @brief オープニング演出中かどうか
	/// @return 演出中の場合true
	bool IsOpeningPlaying() const { 
		return currentState_ != CameraState::FollowPlayer && 
		       currentState_ != CameraState::BossDefeatFocus && 
		       currentState_ != CameraState::PlayerDeathFocus;
	}

	/// @brief ボス撃破演出が終了しているか
	/// @return 終了している場合true
	bool IsBossDefeatFinished() const;

	/// @brief プレイヤー死亡演出が終了しているか
	/// @return 終了している場合true
	bool IsPlayerDeathFinished() const;

	/// @brief 演出終了時のコールバックを設定
	/// @param callback コールバック関数
	void SetOnOpeningFinishedCallback(std::function<void()> callback) {
		onOpeningFinishedCallback_ = callback;
	}

	/// @brief 撃破演出終了時のコールバックを設定
	/// @param callback コールバック関数
	void SetOnBossDefeatFinishedCallback(std::function<void()> callback) {
	 onBossDefeatFinishedCallback_ = callback;
	}

	/// @brief プレイヤー死亡演出終出時のコールバックを設定
	/// @param callback コールバック関数
	void SetOnPlayerDeathFinishedCallback(std::function<void()> callback) {
		onPlayerDeathFinishedCallback_ = callback;
	}

	/// @brief 現在のカメラ状態を取得
	/// @return カメラ状態
	CameraState GetCurrentState() const { return currentState_; }

	/// @brief カメラの向きに対する前方向ベクトルを取得（XZ平面）
	/// @return カメラの前方向（正規化済み）
	CoreEngine::Vector3 GetCameraForward() const;

	/// @brief カメラの向きに対する右方向ベクトルを取得（XZ平面）
	/// @return カメラの右方向（正規化済み）
	CoreEngine::Vector3 GetCameraRight() const;

private:
	/// @brief ボス登場イントロ演出の更新
	void UpdateOpeningBossIntro();

	/// @brief ボス登場演出の更新
	void UpdateOpeningBossFocus();

	/// @brief プレイヤーへの移動演出の更新
	void UpdateOpeningToPlayer();

	/// @brief オープニングフェードアウトの更新
	void UpdateOpeningFadeOut();

	/// @brief オープニングフェードインの更新
	void UpdateOpeningFadeIn();

	/// @brief プレイヤー追従の更新
	void UpdateFollowPlayer();

	/// @brief ボス撃破演出の更新
	void UpdateBossDefeatFocus();

	/// @brief プレイヤー死亡演出の更新
	void UpdatePlayerDeathFocus();

	/// @brief カメラの位置と注視点を設定
	/// @param position カメラ位置
	/// @param target 注視点
	void SetCameraPositionAndTarget(const CoreEngine::Vector3& position, const CoreEngine::Vector3& target);

	// メンバ変数
	CoreEngine::EngineSystem* engine_ = nullptr;
	CoreEngine::ICamera* camera_ = nullptr;
	PlayerObject* player_ = nullptr;
	BossObject* boss_ = nullptr;

	// カメラ状態
	CameraState currentState_ = CameraState::OpeningBossFocus;

	// タイマー
	CoreEngine::GameTimer bossIntroTimer_;      // ボス登場イントロ演出の持続時間
	CoreEngine::GameTimer bossFocusTimer_;      // ボス登場演出の持続時間
	CoreEngine::GameTimer moveToPlayerTimer_;   // プレイヤーへ移動する時間
	CoreEngine::GameTimer fadeOutTimer_;        // フェードアウトの時間
	CoreEngine::GameTimer fadeInTimer_;         // フェードインの時間
	CoreEngine::GameTimer bossDefeatTimer_;     // ボス撃破演出の持続時間
	CoreEngine::GameTimer playerDeathTimer_;    // プレイヤー死亡演出の持続時間

	// オープニング演出のパラメータ
	float bossIntroDuration_ = 2.0f;        // ボス登場イントロ演出の時間（秒）
	float bossFocusDuration_ = 3.0f;        // ボス登場演出の時間（秒）
	float moveToPlayerDuration_ = 2.5f;     // プレイヤーへ移動する時間（秒）
	float fadeDuration_ = 0.5f;             // フェードの時間（秒）

	// ボス撃破演出のパラメータ
	float bossDefeatFocusDuration_ = 3.0f;  // ボス撃破時のフォーカス時間（秒）
	CoreEngine::Vector3 bossDefeatCameraOffset_ = { 5.0f, 4.0f, -12.0f };  // ボス撃破時のカメラオフセット（右斜め後ろから見下ろす）

	// ボス登場演出時のカメラパラメータ
	CoreEngine::Vector3 bossOpeningCameraOffset_ = { 0.0f, 10.0f, -25.0f };  // ボスを遠くから見る位置
	CoreEngine::Vector3 bossOpeningCameraTargetOffset_ = { 0.0f, 3.0f, 0.0f };  // ボスの上半身を注視

	// プレイヤー追従時のカメラパラメータ
	CoreEngine::Vector3 playerCameraOffset_ = { 0.0f, 8.0f, -15.0f }; // プレイヤーからのオフセット
	float cameraFollowSpeed_ = 5.0f;                      // カメラの追従速度

	// 死亡演出時のカメラパラメータ
	CoreEngine::Vector3 playerDeathCameraOffset_ = { 3.0f, 4.0f, -8.0f }; // プレイヤー死亡時のカメラオフセット（右斜め上から見下ろす）
	float playerDeathFocusDuration_ = 3.0f;                   // プレイヤー死亡フォーカス時間（秒）

	// イージング設定
	CoreEngine::EasingUtil::Type easingType_ = CoreEngine::EasingUtil::Type::EaseInOutCubic;

	// 演出開始時の保存データ
	CoreEngine::Vector3 transitionStartPosition_;   // 移動開始時のカメラ位置
	CoreEngine::Vector3 transitionStartTarget_;     // 移動開始時の注視点

	// コールバック
	std::function<void()> onOpeningFinishedCallback_;
	std::function<void()> onBossDefeatFinishedCallback_;
	std::function<void()> onPlayerDeathFinishedCallback_;

	// カメラ方向キャッシュ（入力処理用）
	CoreEngine::Vector3 currentCameraForward_ = { 0.0f, 0.0f, 1.0f };
	CoreEngine::Vector3 currentCameraRight_ = { 1.0f, 0.0f, 0.0f };
};

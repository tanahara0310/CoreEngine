#pragma once

#include <functional>
#include <vector>
#include <string>
#include "Engine/Math/Easing/EasingUtil.h"

/// @brief ゲーム用タイマークラス
/// シーン遷移、ゲーム内演出のタイミング制御に使用（イージングは分離）

namespace CoreEngine
{
class GameTimer {
public:
	/// @brief コールバック情報
	struct TimerCallback {
		float triggerTime;                          // 発火時間
		std::function<void()> callback;             // コールバック関数
		bool triggered = false;                     // 既に発火したか
	};

	/// @brief 繰り返しコールバック情報
	struct RepeatingCallback {
		float interval;                             // 繰り返し間隔
		std::function<void()> callback;             // コールバック関数
		float nextTriggerTime;                      // 次の発火時間
		int executionCount = 0;                     // 実行回数
	};

	/// @brief 間隔チェッカー情報
	struct IntervalChecker {
		float interval;                             // チェック間隔
		float lastCheckTime;                        // 最後にチェックした時間
		int count = 0;                              // 経過回数
	};

	/// @brief デフォルトコンストラクタ
	GameTimer() = default;

	/// @brief コンストラクタ
	/// @param duration タイマーの継続時間（秒）
	/// @param loop ループするかどうか
	GameTimer(float duration, bool loop = false);

	/// @brief タイマーの更新
	/// @param deltaTime 前フレームからの経過時間（秒）
	void Update(float deltaTime);

	/// @brief タイマーを開始
	/// @param duration タイマーの継続時間（秒）
	/// @param loop ループするかどうか
	void Start(float duration, bool loop = false);

	/// @brief タイマーを停止
	void Stop();

	/// @brief タイマーをリセット（停止状態にして時間を0に戻す）
	void Reset();

	/// @brief タイマーを一時停止
	void Pause();

	/// @brief タイマーを再開
	void Resume();

	/// @brief タイマーが動作中かどうか
	/// @return アクティブな場合true
	bool IsActive() const;

	/// @brief タイマーが完了したかどうか
	/// @return 完了した場合true
	bool IsFinished() const;

	/// @brief タイマーの進行状況を取得（0.0～1.0）
	/// @return 進行状況（0.0=開始、1.0=完了）
	float GetProgress() const;

	/// @brief イージング進行状況を取得
	/// @param easingType イージングタイプ
	/// @return イージング適用済み進行状況
	float GetEasedProgress(EasingUtil::Type easingType = EasingUtil::Type::Linear) const;

	/// @brief 残り時間を取得
	/// @return 残り時間（秒）
	float GetRemainingTime() const;

	/// @brief 経過時間を取得
	/// @return 経過時間（秒）
	float GetElapsedTime() const;

	/// @brief タイマーの継続時間を取得
	/// @return 継続時間（秒）
	float GetDuration() const;

	/// @brief タイマーがループ設定かどうか
	/// @return ループ設定の場合true
	bool IsLoop() const;

	/// @brief このフレームでタイマーがループしたかどうか
	/// @return ループした場合true
	bool HasLooped() const;

	/// @brief 継続時間を変更（実行中でも変更可能）
	/// @param duration 新しい継続時間（秒）
	void SetDuration(float duration);

	/// @brief ループ設定を変更
	/// @param loop ループするかどうか
	void SetLoop(bool loop);

	// フレームカウンター機能

	/// @brief フレーム単位でタイマーを開始
	/// @param frameCount フレーム数
	/// @param loop ループするかどうか
	/// @param targetFPS 目標FPS（デフォルト60）
	void StartFrames(int frameCount, bool loop = false, float targetFPS = 60.0f);

	/// @brief 現在のフレーム数を取得
	/// @return フレーム数
	int GetCurrentFrame() const;

	/// @brief 総フレーム数を取得
	/// @return 総フレーム数
	int GetTotalFrames() const;

	// タイムスケール機能

	/// @brief タイムスケールを設定
	/// @param scale タイムスケール（1.0=通常、0.5=半分速度、2.0=倍速）
	void SetTimeScale(float scale);

	/// @brief タイムスケールを取得
	/// @return タイムスケール
	float GetTimeScale() const;

	// コールバック機能

	/// @brief 指定時間でコールバックを追加
	/// @param triggerTime 発火時間（秒）
	/// @param callback コールバック関数
	void AddCallback(float triggerTime, std::function<void()> callback);

	/// @brief 進行率でコールバックを追加
	/// @param progress 発火進行率（0.0～1.0）
	/// @param callback コールバック関数
	void AddCallbackAtProgress(float progress, std::function<void()> callback);

	/// @brief 全コールバックをクリア
	void ClearCallbacks();

	// インターバル・完了時コールバック機能

	/// @brief 指定間隔で繰り返し実行されるコールバックを追加
	/// @param interval 繰り返し間隔（秒）
	/// @param callback コールバック関数
	void AddRepeatingCallback(float interval, std::function<void()> callback);

	/// @brief フレーム間隔で繰り返し実行されるコールバックを追加
	/// @param frameInterval フレーム間隔
	/// @param callback コールバック関数
	/// @param targetFPS 目標FPS（デフォルト60）
	void AddRepeatingCallbackFrames(int frameInterval, std::function<void()> callback, float targetFPS = 60.0f);

	/// @brief タイマー完了時に実行されるコールバックを設定
	/// @param callback コールバック関数
	void SetOnComplete(std::function<void()> callback);

	/// @brief ループ完了時（1周ごと）に実行されるコールバックを設定
	/// @param callback コールバック関数
	void SetOnLoopComplete(std::function<void()> callback);

	/// @brief 全ての繰り返しコールバックをクリア
	void ClearRepeatingCallbacks();

	// 間隔チェック機能

	/// @brief 指定間隔が経過したかをチェック（手動チェック用）
	/// @param interval チェック間隔（秒）
	/// @return 間隔が経過した場合true
	bool CheckInterval(float interval);

	/// @brief フレーム間隔が経過したかをチェック
	/// @param frameInterval フレーム間隔
	/// @param targetFPS 目標FPS（デフォルト60）
	/// @return 間隔が経過した場合true
	bool CheckIntervalFrames(int frameInterval, float targetFPS = 60.0f);

	/// @brief 指定間隔の経過回数を取得
	/// @param interval 間隔（秒）
	/// @return 経過回数
	int GetIntervalCount(float interval);

	/// @brief 間隔チェッカーをリセット
	void ResetIntervalCheckers();


#ifdef _DEBUG
	/// @brief ImGuiでタイマー状態を表示（デバッグビルドのみ）
	/// @param label タイマーのラベル
	void DrawImGui(const char* label = "Timer");
#endif

	/// @brief タイマー名を設定（デバッグ用）
	/// @param name タイマー名
	void SetName(const char* name);

private:
	float currentTime_ = 0.0f;          ///< 現在の経過時間
	float duration_ = 0.0f;             ///< タイマーの継続時間
	bool isActive_ = false;             ///< タイマーがアクティブかどうか
	bool loop_ = false;                 ///< ループするかどうか
	bool finished_ = false;             ///< タイマーが完了したかどうか
	bool loopedThisFrame_ = false;      ///< このフレームでループしたかどうか

	// タイムスケール機能
	float timeScale_ = 1.0f;            ///< タイムスケール

	// フレーム機能
	int totalFrames_ = 0;               ///< 総フレーム数（フレーム単位使用時）
	bool useFrameMode_ = false;         ///< フレーム単位モードかどうか
	float targetFPS_ = 60.0f;           ///< 目標FPS（フレーム単位モード用）

	// コールバック・デバッグ機能
	std::vector<TimerCallback> callbacks_;  ///< コールバックリスト
	std::string name_ = "Timer";             ///< タイマー名（デバッグ用）

	// インターバル・完了時コールバック機能
	std::vector<RepeatingCallback> repeatingCallbacks_;  ///< 繰り返しコールバックリスト
	std::function<void()> onCompleteCallback_;           ///< 完了時コールバック
	std::function<void()> onLoopCompleteCallback_;       ///< ループ完了時コールバック

	// 間隔チェック機能
	std::vector<IntervalChecker> intervalCheckers_;      ///< 間隔チェッカーリスト

	/// @brief コールバックをチェックして実行
	void CheckAndExecuteCallbacks();

	/// @brief 繰り返しコールバックをチェックして実行
	void CheckAndExecuteRepeatingCallbacks();

	/// @brief 完了時コールバックを実行
	void ExecuteOnCompleteCallback();

	/// @brief ループ完了時コールバックを実行
	void ExecuteOnLoopCompleteCallback();

	/// @brief 間隔チェッカーを検索または作成
	/// @param interval 間隔
	/// @return 間隔チェッカーへのポインタ
	IntervalChecker* FindOrCreateIntervalChecker(float interval);
	
	/// @brief フレーム数を時間に変換
	/// @param frameCount フレーム数
	/// @param fps フレームレート
	/// @return 時間（秒）
	template<typename T>
	static constexpr float FramesToSeconds(T frameCount, float fps = 60.0f) {
		return static_cast<float>(frameCount) / fps;
	}
};
}

#include "GameTimer.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include <imgui.h>
#endif


namespace CoreEngine
{
GameTimer::GameTimer(float duration, bool loop) 
	: duration_(duration), loop_(loop) {
}

void GameTimer::Update(float deltaTime) {
	if (!isActive_) return;
	
	// 前フレームのループフラグをリセット
	loopedThisFrame_ = false;
	
	// タイムスケールを適用
	float scaledDeltaTime = deltaTime * timeScale_;
	currentTime_ += scaledDeltaTime;
	
	// コールバックをチェック
	CheckAndExecuteCallbacks();
	
	// 繰り返しコールバックをチェック
	CheckAndExecuteRepeatingCallbacks();
	
	if (currentTime_ >= duration_) {
		finished_ = true;
		
		// 完了時コールバックを実行（ループしない場合のみ）
		if (!loop_) {
			ExecuteOnCompleteCallback();
		}
		
		if (loop_) {
			currentTime_ = 0.0f;
			finished_ = false;  // ループ時は完了フラグをリセット
			loopedThisFrame_ = true;  // ループ発生をマーク
			
			// ループ完了時コールバックを実行
			ExecuteOnLoopCompleteCallback();
			
			// ループ時はコールバックの発火状態もリセット
			for (auto& callback : callbacks_) {
				callback.triggered = false;
			}
			
			// 繰り返しコールバックの次回発火時間を調整
			for (auto& rc : repeatingCallbacks_) {
				rc.nextTriggerTime = rc.interval;
			}
		} else {
			isActive_ = false;
		}
	}
}

void GameTimer::Start(float duration, bool loop) {
	duration_ = duration;
	loop_ = loop;
	currentTime_ = 0.0f;
	isActive_ = true;
	finished_ = false;
	useFrameMode_ = false;
	loopedThisFrame_ = false;  // ループフラグもリセット
	
	// コールバックの発火状態をリセット
	for (auto& callback : callbacks_) {
		callback.triggered = false;
	}
	
	// 繰り返しコールバックをリセット
	for (auto& rc : repeatingCallbacks_) {
		rc.nextTriggerTime = rc.interval;
		rc.executionCount = 0;
	}
	
	// 間隔チェッカーをリセット
	ResetIntervalCheckers();
}

void GameTimer::Stop() {
	isActive_ = false;
}

void GameTimer::Reset() {
	currentTime_ = 0.0f;
	isActive_ = false;
	finished_ = false;
	loopedThisFrame_ = false;  // ループフラグもリセット
	
	// コールバックの発火状態をリセット
	for (auto& callback : callbacks_) {
		callback.triggered = false;
	}
	
	// 繰り返しコールバックをリセット
	for (auto& rc : repeatingCallbacks_) {
		rc.nextTriggerTime = rc.interval;
		rc.executionCount = 0;
	}
	
	// 間隔チェッカーをリセット
	ResetIntervalCheckers();
}

void GameTimer::Pause() {
	isActive_ = false;
}

void GameTimer::Resume() {
	if (currentTime_ < duration_) {
		isActive_ = true;
		finished_ = false;
	}
}

bool GameTimer::IsActive() const {
	return isActive_;
}

bool GameTimer::IsFinished() const {
	return finished_;
}

float GameTimer::GetProgress() const {
	if (duration_ <= 0.0f) return 1.0f;
	return (std::min)(1.0f, currentTime_ / duration_);
}

float GameTimer::GetEasedProgress(EasingUtil::Type easingType) const {
	float progress = GetProgress();
	return EasingUtil::Apply(progress, easingType);
}

float GameTimer::GetRemainingTime() const {
	return (std::max)(0.0f, duration_ - currentTime_);
}

float GameTimer::GetElapsedTime() const {
	return currentTime_;
}

float GameTimer::GetDuration() const {
	return duration_;
}

bool GameTimer::IsLoop() const {
	return loop_;
}

bool GameTimer::HasLooped() const {
	return loopedThisFrame_;
}

void GameTimer::SetDuration(float duration) {
	duration_ = duration;
	// 現在時間が新しい継続時間を超えている場合の処理
	if (currentTime_ >= duration_ && isActive_) {
		finished_ = true;
		if (!loop_) {
			isActive_ = false;
		}
	}
}

void GameTimer::SetLoop(bool loop) {
	loop_ = loop;
}

void GameTimer::StartFrames(int frameCount, bool loop, float targetFPS) {
	totalFrames_ = frameCount;
	targetFPS_ = targetFPS;
	duration_ = FramesToSeconds(frameCount, targetFPS);
	loop_ = loop;
	currentTime_ = 0.0f;
	isActive_ = true;
	finished_ = false;
	useFrameMode_ = true;
	loopedThisFrame_ = false;
	
	// コールバックの発火状態をリセット
	for (auto& callback : callbacks_) {
		callback.triggered = false;
	}
	
	// 繰り返しコールバックをリセット
	for (auto& rc : repeatingCallbacks_) {
		rc.nextTriggerTime = rc.interval;
		rc.executionCount = 0;
	}
	
	// 間隔チェッカーをリセット
	ResetIntervalCheckers();
}

int GameTimer::GetCurrentFrame() const {
	if (!useFrameMode_) return 0;
	return static_cast<int>(currentTime_ * targetFPS_);
}

int GameTimer::GetTotalFrames() const {
	return totalFrames_;
}

void GameTimer::SetTimeScale(float scale) {
	timeScale_ = (std::max)(0.0f, scale);
}

float GameTimer::GetTimeScale() const {
	return timeScale_;
}

void GameTimer::AddCallback(float triggerTime, std::function<void()> callback) {
	TimerCallback timerCallback;
	timerCallback.triggerTime = triggerTime;
	timerCallback.callback = callback;
	timerCallback.triggered = false;
	callbacks_.push_back(timerCallback);
}

void GameTimer::AddCallbackAtProgress(float progress, std::function<void()> callback) {
	float triggerTime = duration_ * progress;
	AddCallback(triggerTime, callback);
}

void GameTimer::ClearCallbacks() {
	callbacks_.clear();
}

void GameTimer::AddRepeatingCallback(float interval, std::function<void()> callback) {
	RepeatingCallback rc;
	rc.interval = interval;
	rc.callback = callback;
	rc.nextTriggerTime = interval;
	rc.executionCount = 0;
	repeatingCallbacks_.push_back(rc);
}

void GameTimer::AddRepeatingCallbackFrames(int frameInterval, std::function<void()> callback, float targetFPS) {
	float intervalSeconds = FramesToSeconds(frameInterval, targetFPS);
	AddRepeatingCallback(intervalSeconds, callback);
}

void GameTimer::SetOnComplete(std::function<void()> callback) {
	onCompleteCallback_ = callback;
}

void GameTimer::SetOnLoopComplete(std::function<void()> callback) {
	onLoopCompleteCallback_ = callback;
}

void GameTimer::ClearRepeatingCallbacks() {
	repeatingCallbacks_.clear();
}

bool GameTimer::CheckInterval(float interval) {
	if (!isActive_ || interval <= 0.0f) return false;
	
	IntervalChecker* checker = FindOrCreateIntervalChecker(interval);
	
	// 前回チェックからの経過時間を計算
	if (currentTime_ >= checker->lastCheckTime + interval) {
		checker->lastCheckTime = currentTime_;
		checker->count++;
		return true;
	}
	
	return false;
}

bool GameTimer::CheckIntervalFrames(int frameInterval, float targetFPS) {
	float intervalSeconds = FramesToSeconds(frameInterval, targetFPS);
	return CheckInterval(intervalSeconds);
}

int GameTimer::GetIntervalCount(float interval) {
	if (interval <= 0.0f) return 0;
	
	IntervalChecker* checker = FindOrCreateIntervalChecker(interval);
	return checker->count;
}

void GameTimer::ResetIntervalCheckers() {
	for (auto& checker : intervalCheckers_) {
		checker.lastCheckTime = 0.0f;
		checker.count = 0;
	}
}

#ifdef _DEBUG
void GameTimer::DrawImGui(const char* label) {
	ImGui::PushID(this);  // 複数のタイマーがある場合のID衝突を防ぐ
	
	if (ImGui::CollapsingHeader(label)) {
		ImGui::Text("Name: %s", name_.c_str());
		ImGui::Text("Status: %s", isActive_ ? "ACTIVE" : (finished_ ? "FINISHED" : "STOPPED"));
		
		// 基本情報
		ImGui::Separator();
		ImGui::Text("Time: %.3f / %.3f sec", currentTime_, duration_);
		ImGui::Text("Progress: %.1f%%", GetProgress() * 100.0f);
		ImGui::Text("Remaining: %.3f sec", GetRemainingTime());
		
		// プログレスバー
		ImGui::ProgressBar(GetProgress(), ImVec2(-1.0f, 0.0f));
		
		// フレームモード情報
		if (useFrameMode_) {
			ImGui::Separator();
			ImGui::Text("Frame Mode: %d / %d frames", GetCurrentFrame(), totalFrames_);
			ImGui::Text("Target FPS: %.1f", targetFPS_);
		}
		
		// タイムスケール
		ImGui::Separator();
		ImGui::Text("Time Scale: %.2fx", timeScale_);
		if (ImGui::SliderFloat("##TimeScale", &timeScale_, 0.0f, 3.0f, "%.2fx")) {
			SetTimeScale(timeScale_);
		}
		
		// 制御ボタン
		ImGui::Separator();
		if (ImGui::Button("Start")) { Start(duration_, loop_); }
		ImGui::SameLine();
		if (ImGui::Button("Stop")) { Stop(); }
		ImGui::SameLine();
		if (ImGui::Button("Reset")) { Reset(); }
		
		if (isActive_) {
			if (ImGui::Button("Pause")) { Pause(); }
		} else if (currentTime_ < duration_) {
			if (ImGui::Button("Resume")) { Resume(); }
		}
		
		// ループ設定とループ状態表示
		ImGui::Checkbox("Loop", &loop_);
		if (loop_) {
			ImGui::SameLine();
			if (loopedThisFrame_) {
				ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[LOOPED THIS FRAME]");
			} else {
				ImGui::Text("[Loop Enabled]");
			}
		}
		
		// コールバック情報
		if (!callbacks_.empty()) {
			ImGui::Separator();
			ImGui::Text("Callbacks: %zu", callbacks_.size());
			for (size_t i = 0; i < callbacks_.size(); ++i) {
				const auto& cb = callbacks_[i];
				ImGui::Text("  [%zu] %.3fs %s", i, cb.triggerTime, cb.triggered ? "(FIRED)" : "");
			}
		}
		
		// 繰り返しコールバック情報
		if (!repeatingCallbacks_.empty()) {
			ImGui::Separator();
			ImGui::Text("Repeating Callbacks: %zu", repeatingCallbacks_.size());
			for (size_t i = 0; i < repeatingCallbacks_.size(); ++i) {
				const auto& rc = repeatingCallbacks_[i];
				ImGui::Text("  [%zu] Interval: %.3fs, Count: %d", i, rc.interval, rc.executionCount);
			}
		}
		
		// 完了時コールバック情報
		if (onCompleteCallback_) {
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "OnComplete Callback: Set");
		}
		
		if (onLoopCompleteCallback_) {
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "OnLoopComplete Callback: Set");
		}
		
		// 間隔チェッカー情報
		if (!intervalCheckers_.empty()) {
			ImGui::Separator();
			ImGui::Text("Interval Checkers: %zu", intervalCheckers_.size());
			for (size_t i = 0; i < intervalCheckers_.size(); ++i) {
				const auto& checker = intervalCheckers_[i];
				ImGui::Text("  [%zu] Interval: %.3fs, Count: %d", i, checker.interval, checker.count);
			}
		}
	}
	
	ImGui::PopID();
}
#endif

void GameTimer::SetName(const char* name) {
	name_ = name;
}

void GameTimer::CheckAndExecuteCallbacks() {
	for (auto& callback : callbacks_) {
		if (!callback.triggered && currentTime_ >= callback.triggerTime) {
			callback.triggered = true;
			if (callback.callback) {
				callback.callback();
			}
		}
	}
}

void GameTimer::CheckAndExecuteRepeatingCallbacks() {
	for (auto& rc : repeatingCallbacks_) {
		if (currentTime_ >= rc.nextTriggerTime) {
			if (rc.callback) {
				rc.callback();
			}
			rc.executionCount++;
			rc.nextTriggerTime += rc.interval;
		}
	}
}

void GameTimer::ExecuteOnCompleteCallback() {
	if (onCompleteCallback_) {
		onCompleteCallback_();
	}
}

void GameTimer::ExecuteOnLoopCompleteCallback() {
	if (onLoopCompleteCallback_) {
		onLoopCompleteCallback_();
	}
}

GameTimer::IntervalChecker* GameTimer::FindOrCreateIntervalChecker(float interval) {
	// 既存のチェッカーを検索（誤差を考慮）
	const float epsilon = 0.0001f;
	for (auto& checker : intervalCheckers_) {
		if (std::abs(checker.interval - interval) < epsilon) {
			return &checker;
		}
	}
	
	// 新しいチェッカーを作成
	IntervalChecker newChecker;
	newChecker.interval = interval;
	newChecker.lastCheckTime = 0.0f;
	newChecker.count = 0;
	intervalCheckers_.push_back(newChecker);
	
	return &intervalCheckers_.back();
}
}

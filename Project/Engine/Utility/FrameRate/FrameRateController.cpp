#include "FrameRateController.h"
#include <algorithm>


namespace CoreEngine
{
void FrameRateController::Initialize()
{
	// 時刻の初期化
	lastFrameTime_ = std::chrono::high_resolution_clock::now();

	// デルタタイムの初期化
	deltaTime_ = kFixedDeltaTime;

	// FPS計測用の初期化
	std::fill(fpsSamples_, fpsSamples_ + kFPSSampleCount, kTargetFPS);
	fpsSampleIndex_ = 0;
	validSampleCount_ = 0;
	currentFPS_ = kTargetFPS;
	frameCount_ = 0;
}

void FrameRateController::BeginFrame()
{
	// 現在時刻を取得
	auto currentTime = std::chrono::high_resolution_clock::now();

	// フレームカウンターをインクリメント
	frameCount_++;

	// 初回フレームは計測をスキップ（起動直後の異常値を避ける）
	if (frameCount_ <= kWarmupFrames) {
		deltaTime_ = kFixedDeltaTime;
		lastFrameTime_ = currentTime;
		// FPS表示は60で固定
		currentFPS_ = kTargetFPS;
		return;
	}

	// 前フレームからの実測経過時間を計算（VSync待機時間を含む）
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
		currentTime - lastFrameTime_
	);
	float actualDeltaTime = duration.count() / 1000000.0f;

	// デルタタイムを更新（異常値のガード）
	if (actualDeltaTime > 0.0001f && actualDeltaTime < 1.0f) {
		deltaTime_ = actualDeltaTime;
	} else {
		// 異常値の場合は固定値を使用
		deltaTime_ = kFixedDeltaTime;
	}

	// FPS計測を更新
	UpdateFPSCalculation();

	// 次フレームのために現在時刻を保存
	lastFrameTime_ = currentTime;
}

void FrameRateController::ResetFPSMeasurement()
{
	// FPS計測をリセット（シーン切り替え時などに使用）
	std::fill(fpsSamples_, fpsSamples_ + kFPSSampleCount, kTargetFPS);
	fpsSampleIndex_ = 0;
	validSampleCount_ = 0;
	currentFPS_ = kTargetFPS;
	frameCount_ = 0;

	// 時刻を現在にリセット
	lastFrameTime_ = std::chrono::high_resolution_clock::now();
}

void FrameRateController::UpdateFPSCalculation()
{
	// 実測FPSを計算（異常値のガード）
	float instantFPS = kTargetFPS;
	if (deltaTime_ > 0.0001f) { // 0除算を防ぐ
		instantFPS = 1.0f / deltaTime_;

		// VSync有効時は目標FPSを超えることはないはずなので、上限を設定
		// 計測誤差を考慮して少し余裕を持たせる（+5%）
		float maxAllowedFPS = kTargetFPS * 1.05f;
		instantFPS = std::clamp(instantFPS, 1.0f, maxAllowedFPS);
	}

	// サンプル配列を更新
	fpsSamples_[fpsSampleIndex_] = instantFPS;
	fpsSampleIndex_ = (fpsSampleIndex_ + 1) % kFPSSampleCount;

	// 有効なサンプル数を更新
	if (validSampleCount_ < kFPSSampleCount) {
		validSampleCount_++;
	}

	// 移動平均を計算
	float totalFPS = 0.0f;
	for (int i = 0; i < validSampleCount_; ++i) {
		totalFPS += fpsSamples_[i];
	}
	currentFPS_ = totalFPS / validSampleCount_;

	// 最終的な表示値も目標FPSでクランプ（表示上の安定性向上）
	currentFPS_ = std::min(currentFPS_, kTargetFPS);
}
}

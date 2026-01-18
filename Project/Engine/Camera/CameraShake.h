#pragma once

#include "MathCore.h"
#include "Engine/Utility/Random/RandomGenerator.h"
#include "Engine/Utility/Timer/GameTimer.h"

/// @brief カメラシェイククラス
/// @details カメラに揺れを追加して臨場感を演出

namespace CoreEngine
{
class CameraShake {
public:
	CameraShake() = default;
	~CameraShake() = default;

	/// @brief シェイクを開始
	/// @param duration 持続時間（秒）
	/// @param intensity 揺れの強度
	/// @param frequency 揺れの周波数
	void Start(float duration, float intensity, float frequency = 10.0f);

	/// @brief シェイクを停止
	void Stop();

	/// @brief 更新処理
	/// @param deltaTime フレーム時間
	void Update(float deltaTime);

	/// @brief シェイクのオフセットを取得
	/// @return カメラに適用するオフセット
	Vector3 GetOffset() const { return currentOffset_; }

	/// @brief シェイクがアクティブか
	/// @return アクティブならtrue
	bool IsActive() const { return shakeTimer_.IsActive(); }

	/// @brief 減衰カーブを設定（1.0 = 線形、2.0 = 二次曲線）
	/// @param curve 減衰カーブの指数
	void SetDecayCurve(float curve) { decayCurve_ = curve; }

private:
	/// @brief ランダムなオフセットを生成
	Vector3 GenerateRandomOffset(float amplitude);

	float intensity_ = 0.0f;
	float frequency_ = 10.0f;
	float decayCurve_ = 2.0f;  // 減衰カーブ（デフォルトは二次曲線）

	Vector3 currentOffset_ = { 0.0f, 0.0f, 0.0f };

	GameTimer shakeTimer_;                      // シェイクの持続時間管理
	GameTimer shakeIntervalTimer_;              // 揺れの間隔管理
	RandomGenerator& random_ = RandomGenerator::GetInstance();  // 乱数生成器
};
}

#include "AnimationUtils.h"
#include <Math/MathCore.h>
#include <cassert>
#include <algorithm>


namespace CoreEngine
{
namespace AnimationUtils {

namespace {
	/// @brief 2つのキーフレーム間の補間係数を計算
	/// @param t1 開始時刻
	/// @param t2 終了時刻
	/// @param currentTime 現在時刻
	/// @return 補間係数（0.0 ~ 1.0）
	inline float CalculateInterpolationFactor(float t1, float t2, float currentTime) {
		return (currentTime - t1) / (t2 - t1);
	}
}

Vector3 CalculateVector3(const std::vector<Keyframe<Vector3>>& keyframes, float time) {
	assert(!keyframes.empty());
	
	// 単一キーフレームまたは範囲外の場合
	if (keyframes.size() == 1 || time <= keyframes.front().time) {
		return keyframes.front().value;
	}
	if (time >= keyframes.back().time) {
		return keyframes.back().value;
	}
	
	// 二分探索で効率的に検索
	auto it = std::lower_bound(
		keyframes.begin(), 
		keyframes.end(), 
		time,
		[](const Keyframe<Vector3>& kf, float t) { return kf.time < t; }
	);

	if (it == keyframes.begin() || it == keyframes.end()) {
		return keyframes.front().value;
	}

	size_t nextIndex = std::distance(keyframes.begin(), it);
	size_t index = nextIndex - 1;
	
	// 線形補間
	float t = CalculateInterpolationFactor(
		keyframes[index].time, 
		keyframes[nextIndex].time, 
		time
	);
	
	return keyframes[index].value + (keyframes[nextIndex].value - keyframes[index].value) * t;
}

Quaternion CalculateQuaternion(const std::vector<Keyframe<Quaternion>>& keyframes, float time) {
	assert(!keyframes.empty());
	
	// 単一キーフレームまたは範囲外の場合
	if (keyframes.size() == 1 || time <= keyframes.front().time) {
		return keyframes.front().value;
	}
	if (time >= keyframes.back().time) {
		return keyframes.back().value;
	}
	
	// 二分探索で効率的に検索
	auto it = std::lower_bound(
		keyframes.begin(), 
		keyframes.end(), 
		time,
		[](const Keyframe<Quaternion>& kf, float t) { return kf.time < t; }
	);

	if (it == keyframes.begin() || it == keyframes.end()) {
		return keyframes.front().value;
	}

	size_t nextIndex = std::distance(keyframes.begin(), it);
	size_t index = nextIndex - 1;
	
	// 球面線形補間
	float t = CalculateInterpolationFactor(
		keyframes[index].time, 
		keyframes[nextIndex].time, 
		time
	);
	
	return MathCore::QuaternionMath::Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
}

} // namespace AnimationUtils
}

#pragma once

#include <Math/Vector/Vector3.h>
#include <Math/Quaternion/Quaternion.h>
#include <vector>
#include "NodeAnimation.h"

/// @brief アニメーション補間ユーティリティ

namespace CoreEngine
{
namespace AnimationUtils {

/// @brief Vector3のキーフレーム配列から任意の時刻の値を計算
/// @param keyframes キーフレーム配列
/// @param time 時刻
/// @return 補間された値
Vector3 CalculateVector3(const std::vector<Keyframe<Vector3>>& keyframes, float time);

/// @brief Quaternionのキーフレーム配列から任意の時刻の値を計算
/// @param keyframes キーフレーム配列
/// @param time 時刻
/// @return 補間された値
Quaternion CalculateQuaternion(const std::vector<Keyframe<Quaternion>>& keyframes, float time);

} // namespace AnimationUtils
}

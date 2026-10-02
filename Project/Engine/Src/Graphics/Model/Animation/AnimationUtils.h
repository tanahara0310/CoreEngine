#pragma once

#include <Math/Vector/Vector3.h>
#include <Math/Quaternion/Quaternion.h>
#include <vector>
#include "NodeAnimation.h"

/// @file
/// @brief アニメーション補間ユーティリティ

namespace CoreEngine
{
struct Skeleton;

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

/// @brief 2 つの姿勢をジョイントごとに混ぜる（平行移動・スケールは Lerp、回転は Slerp）
/// @param from 混ぜる元の姿勢
/// @param to 混ぜる先の姿勢
/// @param weight to の重み（0.0～1.0）
/// @return 混ぜた姿勢（ジョイントの行列も計算し直したもの）
Skeleton BlendSkeletons(const Skeleton& from, const Skeleton& to, float weight);

} // namespace AnimationUtils
}

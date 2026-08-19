#pragma once
#include "Keyframe.h"
#include <vector>


namespace CoreEngine
{
template<typename tValue>
/// @brief 時刻順に並んだキーフレームの列
struct AnimationCurve {
    std::vector<Keyframe<tValue>> keyframes; //!< キーフレームの配列
};

/// @brief 1 ノード分のアニメーション（平行移動・回転・スケールのカーブ）
struct NodeAnimation {
    AnimationCurve<Vector3> translate;
    AnimationCurve<Quaternion> rotate;
    AnimationCurve<Vector3> scale;

};
}

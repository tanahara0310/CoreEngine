#pragma once
#include "Keyframe.h"
#include <vector>


namespace CoreEngine
{
template<typename tValue>
struct AnimationCurve {
	std::vector<Keyframe<tValue>> keyframes; //!< キーフレームの配列
};

struct NodeAnimation {
	AnimationCurve<Vector3> translate;
	AnimationCurve<Quaternion> rotate;
	AnimationCurve<Vector3> scale;

};
}

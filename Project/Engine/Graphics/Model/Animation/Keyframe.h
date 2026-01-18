#pragma once
#include <Math/Vector/Vector3.h>
#include <Math/Quaternion/Quaternion.h>


namespace CoreEngine
{
template<typename tValue>
struct Keyframe {

	float time;      //!< キーフレームの時刻(単位は秒)
	tValue value;   //!< キーフレームの値

};

using KeyframeVector3 = Keyframe<Vector3>;
using KeyframeQuaternion = Keyframe<Quaternion>;
}

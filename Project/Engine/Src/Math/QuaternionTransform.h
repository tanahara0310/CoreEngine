#pragma once

#include "Math/Vector/Vector3.h"
#include "Quaternion/Quaternion.h"


namespace CoreEngine
{
/// @brief スケール・クォータニオン回転・平行移動の組
struct QuaternionTransform {
    Vector3 scale;
    Quaternion rotate;
    Vector3 translate;
};
}

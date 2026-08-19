#pragma once

#include "Math/Vector/Vector3.h"


namespace CoreEngine
{
/// @brief スケール・オイラー角回転・平行移動の組（回転合成は Rx*Ry*Rz）
struct EulerTransform {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};
}

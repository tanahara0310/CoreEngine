#pragma once
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"

// スプライト専用マテリアル構造体（シェーダーと完全一致）

namespace CoreEngine
{
struct SpriteMaterial {
    Vector4 color;
    Matrix4x4 uvTransform;
};
}

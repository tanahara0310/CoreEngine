#pragma once
#include "Engine/Math/QuaternionTransform.h"
#include "Engine/Math/Matrix/Matrix4x4.h"
#include <string>
#include <vector>
#include <optional>

/// @brief Joint構造体

namespace CoreEngine
{
struct Joint {
    QuaternionTransform transform;        // Transform情報
    Matrix4x4 localMatrix;         // localMatrix
    Matrix4x4 skeletonSpaceMatrix;         // skeletonSpaceでの変換行列
    std::string name;                 // 名前
    std::vector<int32_t> children;         // 子JointのIndexのリスト
    int32_t index;   // 自身のIndex
    std::optional<int32_t> parent;         // 親JointのIndex
};
}

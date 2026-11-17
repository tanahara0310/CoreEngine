#pragma once

#include "Math/QuaternionTransform.h"

#include <cstddef>
#include <vector>

namespace CoreEngine
{
    struct NodeAnimation;

    /// @brief 骨 1 本の姿勢（拡縮・回転・移動）
    using JointPose = QuaternionTransform;

    /// @brief 骨ごとの姿勢の並び（`Skeleton::joints` と同じ順）
    using Pose = std::vector<JointPose>;

    /// @brief 骨の番号 → そのクリップのトラック（無ければ nullptr）
    /// @note 骨の並びとクリップの組ごとに 1 回だけ作る。
    struct ClipBinding
    {
        std::vector<const NodeAnimation*> tracks;
        std::size_t matchedCount = 0;  ///< トラックが見つかった骨の数
    };
}

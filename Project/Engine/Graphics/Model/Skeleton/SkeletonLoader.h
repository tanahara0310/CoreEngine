#pragma once
#include "Skeleton.h"
#include "Engine/Graphics/Model/Node.h"

/// @brief Skeletonローダークラス

namespace CoreEngine
{
class SkeletonLoader {
public:
    /// @brief NodeからSkeletonを作成
    /// @param rootNode ルートNode
    /// @return 作成されたSkeleton
    static Skeleton CreateSkeleton(const Node& rootNode);

private:
    /// @brief NodeからJointを作成（再帰的）
    /// @param node Node
    /// @param parent 親JointのIndex
    /// @param joints Jointのリスト
    /// @return 作成されたJointのIndex
    static int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints);
};
}

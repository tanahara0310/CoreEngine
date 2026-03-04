#include "SkeletonLoader.h"
#include "Math/MathCore.h"


namespace CoreEngine
{
Skeleton SkeletonLoader::CreateSkeleton(const Node& rootNode) {
    Skeleton skeleton;

    // rootNodeからJoint階層を構築
    skeleton.root = CreateJoint(rootNode, {}, skeleton.joints);

    // 名前とIndexのマッピングを行いアクセスしやすくする
    for (const Joint& joint : skeleton.joints) {
        skeleton.jointMap.emplace(joint.name, joint.index);
    }

    // 初期状態でSkeletonの行列を更新
    for (Joint& joint : skeleton.joints) {
        // TransformからlocalMatrixを更新
        joint.localMatrix = MathCore::Matrix::MakeAffine(
            joint.transform.scale,
            joint.transform.rotate,
            joint.transform.translate
        );

        // 親がいれば親の行列を掛ける
        if (joint.parent) {
            joint.skeletonSpaceMatrix = MathCore::Matrix::Multiply(
                joint.localMatrix,
                skeleton.joints[*joint.parent].skeletonSpaceMatrix
            );
        } else {
            // 親がいないのでlocalMatrixとskeletonSpaceMatrixは一致する
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }

    return skeleton;
}

int32_t SkeletonLoader::CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints) {
    Joint joint;
    joint.name = node.name;
    joint.localMatrix = node.localMatrix;
    joint.skeletonSpaceMatrix = MathCore::Matrix::Identity();
    joint.transform = node.transform;
    joint.index = static_cast<int32_t>(joints.size());
    joint.parent = parent;
    joints.push_back(joint);

    // 子Jointを作成し、そのIndexを登録
    for (const Node& child : node.children) {
        int32_t childIndex = CreateJoint(child, joint.index, joints);
        joints[joint.index].children.push_back(childIndex);
    }

    // 自身のIndexを返す
    return joint.index;
}
}

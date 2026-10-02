#include "pch.h"
#include "AnimationUtils.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include <Math/MathCore.h>
#include <cassert>
#include <algorithm>


namespace CoreEngine
{
namespace AnimationUtils {

namespace {
    /// @brief 2 つのキーフレーム間の補間係数（0.0 ~ 1.0）を計算
    inline float CalculateInterpolationFactor(float t1, float t2, float currentTime) {
        return (currentTime - t1) / (t2 - t1);
    }
}

Vector3 CalculateVector3(const std::vector<Keyframe<Vector3>>& keyframes, float time) {
    assert(!keyframes.empty());
    
    // 単一キーフレームまたは範囲外の場合
    if (keyframes.size() == 1 || time <= keyframes.front().time) {
        return keyframes.front().value;
    }
    if (time >= keyframes.back().time) {
        return keyframes.back().value;
    }
    
    // 二分探索で効率的に検索
    auto it = std::lower_bound(
        keyframes.begin(), 
        keyframes.end(), 
        time,
        [](const Keyframe<Vector3>& kf, float t) { return kf.time < t; }
    );

    if (it == keyframes.begin() || it == keyframes.end()) {
        return keyframes.front().value;
    }

    size_t nextIndex = std::distance(keyframes.begin(), it);
    size_t index = nextIndex - 1;
    
    // 線形補間
    float t = CalculateInterpolationFactor(
        keyframes[index].time, 
        keyframes[nextIndex].time, 
        time
    );
    
    return keyframes[index].value + (keyframes[nextIndex].value - keyframes[index].value) * t;
}

Quaternion CalculateQuaternion(const std::vector<Keyframe<Quaternion>>& keyframes, float time) {
    assert(!keyframes.empty());
    
    // 単一キーフレームまたは範囲外の場合
    if (keyframes.size() == 1 || time <= keyframes.front().time) {
        return keyframes.front().value;
    }
    if (time >= keyframes.back().time) {
        return keyframes.back().value;
    }
    
    // 二分探索で効率的に検索
    auto it = std::lower_bound(
        keyframes.begin(), 
        keyframes.end(), 
        time,
        [](const Keyframe<Quaternion>& kf, float t) { return kf.time < t; }
    );

    if (it == keyframes.begin() || it == keyframes.end()) {
        return keyframes.front().value;
    }

    size_t nextIndex = std::distance(keyframes.begin(), it);
    size_t index = nextIndex - 1;
    
    // 球面線形補間
    float t = CalculateInterpolationFactor(
        keyframes[index].time, 
        keyframes[nextIndex].time, 
        time
    );
    
    return MathCore::QuaternionMath::Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
}

Skeleton BlendSkeletons(const Skeleton& from, const Skeleton& to, float weight) {
    Skeleton result = from;

    // 各ジョイントをブレンド
    for (size_t i = 0; i < result.joints.size() && i < to.joints.size(); ++i) {
        Joint& joint = result.joints[i];
        const Joint& toJoint = to.joints[i];

        // 平行移動の線形補間
        joint.transform.translate = MathCore::Lerp(
            joint.transform.translate,
            toJoint.transform.translate,
            weight
        );

        // 回転のSlerp（球面線形補間）
        joint.transform.rotate = MathCore::QuaternionMath::Slerp(
            joint.transform.rotate,
            toJoint.transform.rotate,
            weight
        );

        // スケールの線形補間
        joint.transform.scale = MathCore::Lerp(
            joint.transform.scale,
            toJoint.transform.scale,
            weight
        );

        // TransformからlocalMatrixを更新
        joint.localMatrix = MathCore::Matrix::MakeAffine(
            joint.transform.scale,
            joint.transform.rotate,
            joint.transform.translate
        );

        // 親がいれば親の行列を掛ける
        if (joint.parent) {
            joint.skeletonSpaceMatrix = joint.localMatrix * result.joints[*joint.parent].skeletonSpaceMatrix;
        } else {
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }

    return result;
}

} // namespace AnimationUtils
}

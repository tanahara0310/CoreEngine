#include "pch.h"
#include "PoseEvaluator.h"

#include "Graphics/Model/Animation/Animation.h"
#include "Graphics/Model/Animation/AnimationUtils.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Math/MathCore.h"

namespace CoreEngine::PoseEvaluator
{
    ClipBinding Bind(const Skeleton& skeleton, const Animation& clip)
    {
        ClipBinding binding;
        binding.tracks.resize(skeleton.joints.size(), nullptr);
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            const auto it = clip.nodeAnimations.find(skeleton.joints[i].name);
            if (it != clip.nodeAnimations.end()) {
                binding.tracks[i] = &it->second;
                ++binding.matchedCount;
            }
        }
        return binding;
    }

    void WriteBindPose(const Skeleton& skeleton, Pose& out)
    {
        out.resize(skeleton.joints.size());
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            out[i] = skeleton.joints[i].transform;
        }
    }

    void Sample(const Skeleton& skeleton, const ClipBinding& binding, float time, Pose& out)
    {
        const size_t count = skeleton.joints.size();
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const QuaternionTransform& bind = skeleton.joints[i].transform;
            const NodeAnimation* const track = i < binding.tracks.size() ? binding.tracks[i] : nullptr;
            if (!track) {
                out[i] = bind;
                continue;
            }
            JointPose& joint = out[i];
            joint.translate = track->translate.keyframes.empty()
                ? bind.translate : AnimationUtils::CalculateVector3(track->translate.keyframes, time);
            joint.rotate = track->rotate.keyframes.empty()
                ? bind.rotate : AnimationUtils::CalculateQuaternion(track->rotate.keyframes, time);
            joint.scale = track->scale.keyframes.empty()
                ? bind.scale : AnimationUtils::CalculateVector3(track->scale.keyframes, time);
        }
    }

    void Blend(const Pose& from, const Pose& to, float weight, Pose& out)
    {
        const size_t count = (std::min)(from.size(), to.size());
        out.resize(from.size());
        for (size_t i = 0; i < count; ++i) {
            const JointPose& a = from[i];
            const JointPose& b = to[i];
            JointPose& result = out[i];
            result.translate = MathCore::Lerp(a.translate, b.translate, weight);
            result.rotate = MathCore::QuaternionMath::Slerp(a.rotate, b.rotate, weight);
            result.scale = MathCore::Lerp(a.scale, b.scale, weight);
        }
        for (size_t i = count; i < from.size(); ++i) {
            out[i] = from[i];
        }
    }

    void ComputeSkeletonSpace(const Skeleton& skeleton, std::span<const JointPose> pose, std::vector<Matrix4x4>& out)
    {
        const size_t count = (std::min)(skeleton.joints.size(), pose.size());
        out.resize(skeleton.joints.size());
        for (size_t i = 0; i < count; ++i) {
            const JointPose& joint = pose[i];
            const Matrix4x4 local = MathCore::Matrix::MakeAffine(joint.scale, joint.rotate, joint.translate);
            const std::optional<int32_t>& parent = skeleton.joints[i].parent;
            out[i] = parent ? local * out[static_cast<size_t>(*parent)] : local;
        }
    }
}

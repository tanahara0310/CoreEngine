#pragma once

#include "Animation/Pose.h"
#include "Math/Matrix/Matrix4x4.h"

#include <span>
#include <vector>

namespace CoreEngine
{
    struct Animation;
    struct Skeleton;
}

/// @brief クリップを姿勢に書く・姿勢を混ぜる・姿勢から骨の行列を作る
namespace CoreEngine::PoseEvaluator
{
    /// @brief 骨の並びとクリップから、骨ごとのトラックの表を作る（骨の名前で引く）
    ClipBinding Bind(const Skeleton& skeleton, const Animation& clip);

    /// @brief 骨の初期の姿勢を書く
    void WriteBindPose(const Skeleton& skeleton, Pose& out);

    /// @brief クリップの時刻 time の姿勢を書く
    /// @details トラックの無い骨と、キーの無い曲線は骨の初期の姿勢の値にする。
    void Sample(const Skeleton& skeleton, const ClipBinding& binding, float time, Pose& out);

    /// @brief 2 つの姿勢を骨ごとに混ぜる（移動と拡縮は Lerp・回転は Slerp）
    /// @param weight to の重み（0〜1）
    /// @note out は from と同じ配列でもよい。
    void Blend(const Pose& from, const Pose& to, float weight, Pose& out);

    /// @brief 骨 1 本の姿勢を混ぜる（移動と拡縮は Lerp・回転は Slerp）
    /// @param weight to の重み（0〜1）
    JointPose BlendJoint(const JointPose& from, const JointPose& to, float weight);

    /// @brief 姿勢から骨ごとのスケルトン空間の行列を作る
    /// @note 骨は親が子より前に並んでいるものとして、並びの順に親の行列を掛ける。
    void ComputeSkeletonSpace(const Skeleton& skeleton, std::span<const JointPose> pose, std::vector<Matrix4x4>& out);
}

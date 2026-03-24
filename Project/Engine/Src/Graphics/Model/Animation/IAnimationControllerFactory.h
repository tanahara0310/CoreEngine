#pragma once
#include "IAnimationController.h"
#include <memory>

namespace CoreEngine {
    struct Skeleton;
    struct Animation;
}

namespace CoreEngine
{
    /// @brief アニメーションコントローラー生成のファクトリインターフェース
    /// Model が SkeletonAnimator / AnimationBlender の具体型に依存しないよう
    /// 生成責任をこのインターフェースに集約する（DIP）
    class IAnimationControllerFactory {
    public:
        virtual ~IAnimationControllerFactory() = default;

        /// @brief SkeletonAnimator を生成する
        /// @param skeleton 初期スケルトン状態（コピーして保持）
        /// @param animation 再生するアニメーション
        /// @param loop ループ再生するか
        virtual std::unique_ptr<IAnimationController> CreateSkeletonAnimator(
            const Skeleton& skeleton,
            const Animation& animation,
            bool loop) const = 0;

        /// @brief AnimationBlender を生成し、ブレンド開始状態で返す
        /// @param from 現在のコントローラー（Blender に移譲）
        /// @param to ブレンド先のコントローラー
        /// @param blendDuration ブレンド時間（秒）
        virtual std::unique_ptr<IAnimationController> CreateBlenderWithTarget(
            std::unique_ptr<IAnimationController> from,
            std::unique_ptr<IAnimationController> to,
            float blendDuration) const = 0;
    };
}

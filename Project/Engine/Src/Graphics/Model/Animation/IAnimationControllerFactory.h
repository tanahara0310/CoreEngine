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
    /// Model が SkeletonAnimator の具体型に依存しないよう
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
    };
}

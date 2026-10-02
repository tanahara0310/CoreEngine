#pragma once

#include <Math/Matrix/Matrix4x4.h>
#include <memory>
#include <string>

namespace CoreEngine { struct Skeleton; }

namespace CoreEngine
{
/// @brief アニメーション制御インターフェース
/// Animator と SkeletonAnimator を統一的に扱うための基底クラス
class IAnimationController {
public:
    virtual ~IAnimationController() = default;

    /// @brief アニメーションを更新
    /// @param deltaTime デルタタイム（秒）
    virtual void Update(float deltaTime) = 0;

    /// @brief アニメーション時刻を取得
    /// @return 現在の再生時刻（秒）
    virtual float GetAnimationTime() const = 0;

    /// @brief アニメーションをリセット
    virtual void Reset() = 0;

    /// @brief アニメーションが終了したか確認
    /// @return アニメーションが終了していればtrue
    virtual bool IsFinished() const = 0;

    /// @brief スケルトンを取得（スケルトンアニメーションの場合のみ有効）
    /// @return スケルトンへのポインタ（非対応の場合はnullptr）
    virtual Skeleton* GetSkeleton() { return nullptr; }
    virtual const Skeleton* GetSkeleton() const { return nullptr; }
};
}

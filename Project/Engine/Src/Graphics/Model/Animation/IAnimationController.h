#pragma once

#include <Math/Matrix/Matrix4x4.h>
#include <string>

/// @brief アニメーション制御インターフェース
/// Animator と SkeletonAnimator を統一的に扱うための基底クラス

namespace CoreEngine
{
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
};
}

#pragma once

#include "Math/Vector/Vector3.h"

#include <cstdint>

namespace CoreEngine
{
/// @brief 物理を一定幅の時間で進めるワールド
/// @details 経過時間を溜め、溜まった分を固定ステップへ切り分けて進める。
class PhysicsWorld {
public:
    /// @brief 経過時間を溜め、溜まった分だけステップを進める
    /// @param deltaTime 前フレームからの経過時間（秒）。0 以下は無視する
    /// @return このフレームで進めたステップ数
    /// @note 上限（GetMaxSubSteps）に達したら、残った時間は捨てて次フレームへ持ち越さない。
    int Advance(float deltaTime);

    /// @brief 溜めた時間・ステップ数・シミュレーション時間を初期状態へ戻す
    void Reset();

    // ===== 設定 =====

    /// @brief 1 ステップの秒数を設定する（0 以下は無視する）
    void SetFixedDeltaTime(float seconds);
    float GetFixedDeltaTime() const { return fixedDeltaTime_; }

    /// @brief 1 フレームで進めるステップ数の上限を設定する（1 未満は 1 に丸める）
    void SetMaxSubSteps(int count);
    int GetMaxSubSteps() const { return maxSubSteps_; }

    /// @brief 重力加速度を設定する（m/s²）
    void SetGravity(const Vector3& gravity) { gravity_ = gravity; }
    const Vector3& GetGravity() const { return gravity_; }

    // ===== 状態 =====

    /// @brief 直前の Advance が進めたステップ数
    int GetLastStepCount() const { return lastStepCount_; }

    /// @brief Reset からの累計ステップ数
    uint64_t GetTotalStepCount() const { return totalStepCount_; }

    /// @brief まだステップへ切り分けていない残り時間（秒）
    float GetAccumulator() const { return accumulator_; }

    /// @brief ステップとして進めた時間の累計（秒）
    float GetSimulatedTime() const { return simulatedTime_; }

    /// @brief 上限に達して捨てた時間の累計（秒）
    float GetDroppedTime() const { return droppedTime_; }

private:
    /// @brief 1 ステップ分だけ物理を進める
    void Step(float fixedDeltaTime);

    Vector3 gravity_{ 0.0f, -9.81f, 0.0f };
    float   fixedDeltaTime_ = 1.0f / 60.0f;
    int     maxSubSteps_ = 4;

    float    accumulator_ = 0.0f;
    int      lastStepCount_ = 0;
    uint64_t totalStepCount_ = 0;
    float    simulatedTime_ = 0.0f;
    float    droppedTime_ = 0.0f;
};
}

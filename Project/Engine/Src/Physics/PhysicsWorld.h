#pragma once

#include "ContactSolver.h"

#include "Collision/CollisionWorld.h"
#include "Math/Vector/Vector3.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace CoreEngine
{
class RigidbodyComponent;

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

    // ===== 剛体 =====

    /// @brief 登録されている剛体をすべて外す
    void ClearBodies();

    /// @brief 剛体を登録する（毎フレーム集め直す）
    void RegisterBody(RigidbodyComponent* body);

    /// @brief 登録されている剛体の数
    size_t GetBodyCount() const { return bodies_.size(); }

    /// @brief 接触を集める相手（nullptr なら接触の解決を行わない）
    void SetCollisionWorld(CollisionWorld* world) { collisionWorld_ = world; }

    /// @brief 1 ステップを進める前に呼ぶ処理（スクリプトの FixedUpdate を回す口）
    /// @param callback 引数はそのステップの秒数。空の関数を渡すと呼ばなくなる
    void SetPreStepCallback(std::function<void(float)> callback)
    {
        preStep_ = std::move(callback);
    }

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

    /// @brief めり込みを 1 ステップで押し戻す割合を設定する（0〜1 に丸める）
    void SetCorrectionRate(float rate);
    float GetCorrectionRate() const { return correctionRate_; }

    /// @brief 押し戻さずに許すめり込みの深さを設定する（m。負値は 0 に丸める）
    void SetPenetrationSlop(float slop);
    float GetPenetrationSlop() const { return penetrationSlop_; }

    /// @brief 接触を解く繰り返し回数を設定する
    void SetSolverIterations(int count) { solver_.SetIterations(count); }
    int GetSolverIterations() const { return solver_.GetIterations(); }

    /// @brief この速さ未満の接近では反発させない（m/s）
    void SetRestitutionThreshold(float speed) { solver_.SetRestitutionThreshold(speed); }
    float GetRestitutionThreshold() const { return solver_.GetRestitutionThreshold(); }

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

    /// @brief 直前のステップで解いた接触の数
    size_t GetContactCount() const { return solver_.GetConstraintCount(); }

private:
    /// @brief 1 ステップ分だけ物理を進める
    /// @details 速度の積分 → 接触の収集 → 速度の解決 → 位置の積分 → めり込みの押し戻し。
    ///          速度を先に解決するので、接している剛体は位置を進める前に押し戻しの分だけ止まる。
    void Step(float fixedDeltaTime);

    Vector3 gravity_{ 0.0f, -9.81f, 0.0f };
    float   fixedDeltaTime_ = 1.0f / 60.0f;
    int     maxSubSteps_ = 4;
    float   correctionRate_ = 0.2f;
    float   penetrationSlop_ = 0.01f;

    std::function<void(float)>       preStep_;
    std::vector<RigidbodyComponent*> bodies_;
    CollisionWorld*                  collisionWorld_ = nullptr;
    std::vector<ContactPair>         contacts_;
    ContactSolver                    solver_;

    float    accumulator_ = 0.0f;
    int      lastStepCount_ = 0;
    uint64_t totalStepCount_ = 0;
    float    simulatedTime_ = 0.0f;
    float    droppedTime_ = 0.0f;
};
}

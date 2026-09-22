#pragma once

#include "Math/Vector/Vector3.h"

#include <vector>

namespace CoreEngine
{
struct ContactPair;
class RigidbodyComponent;

/// @brief 1 つの接触を解くための拘束
/// @note normal は bodyA から bodyB へ向かう。インパルスは正の値が「離す向き」。
struct ContactConstraint {
    RigidbodyComponent* bodyA = nullptr;   ///< 剛体を持たない側は nullptr（無限質量）
    RigidbodyComponent* bodyB = nullptr;

    Vector3 normal{};     ///< 接触法線
    Vector3 tangent1{};   ///< 摩擦の軸その 1（法線に直交）
    Vector3 tangent2{};   ///< 摩擦の軸その 2（法線とその 1 に直交）

    Vector3 point{};      ///< 接触点（ワールド）
    Vector3 leverA{};     ///< bodyA の重心から接触点へ
    Vector3 leverB{};     ///< bodyB の重心から接触点へ

    float depth = 0.0f;             ///< 貫通深度
    float inverseMassA = 0.0f;
    float inverseMassB = 0.0f;
    float inverseMassSum = 0.0f;    ///< 押し戻しに使う（並進だけの和）
    float friction = 0.0f;          ///< 合成後の摩擦係数
    float targetSeparation = 0.0f;  ///< 反発で目指す離れる速さ（m/s）

    float normalImpulse = 0.0f;     ///< 法線インパルスの累積（クランプに使う）
    float tangentImpulse1 = 0.0f;
    float tangentImpulse2 = 0.0f;
};

/// @brief 接触を逐次インパルスで解く
/// @details 拘束を組み立て、法線 → 摩擦の順に決められた回数だけ繰り返し解く。
class ContactSolver {
public:
    /// @brief 1 ステップで繰り返す回数を設定する（1 未満は 1 に丸める）
    void SetIterations(int count);
    int GetIterations() const { return iterations_; }

    /// @brief この速さ未満の接近では反発させない（m/s。負値は 0 に丸める）
    void SetRestitutionThreshold(float speed);
    float GetRestitutionThreshold() const { return restitutionThreshold_; }

    /// @brief 接触から拘束を組み立てる
    /// @details トリガー・両方とも無限質量・剛体が片方も無い接触は落とす。
    void Build(const std::vector<ContactPair>& pairs);

    /// @brief 近づく向きの速度を打ち消し、反発と摩擦を与える
    void SolveVelocities();

    /// @brief 許容を超えためり込みを押し戻す
    /// @param correctionRate 1 回で戻す割合
    /// @param slop           押し戻さずに許す深さ（m）
    void SolvePositions(float correctionRate, float slop);

    /// @brief 組み立てた拘束の数
    size_t GetConstraintCount() const { return constraints_.size(); }

private:
    /// @brief 1 本の軸について相対速度を打ち消すインパルスを与える
    /// @param constraint 対象の拘束
    /// @param axis       打ち消す軸
    /// @param accumulated 累積インパルス（クランプのために読み書きする）
    /// @param minImpulse 累積の下限
    /// @param maxImpulse 累積の上限
    /// @param bias       目標にする相対速度（正で離れる向き）
    /// @note 接触点での相対速度を見るので、回転の寄与も込みで打ち消す。
    static void ApplyAxisImpulse(ContactConstraint& constraint, const Vector3& axis,
                                 float& accumulated, float minImpulse, float maxImpulse,
                                 float bias);

    /// @brief 1 本の軸について、接触点に効く質量の逆数を求める
    /// @details 並進の逆質量に、てこの長さと慣性から決まる回りにくさを足したもの。
    static float EffectiveInverseMass(const ContactConstraint& constraint, const Vector3& axis);

    std::vector<ContactConstraint> constraints_;
    int   iterations_ = 8;
    float restitutionThreshold_ = 1.0f;
};
}

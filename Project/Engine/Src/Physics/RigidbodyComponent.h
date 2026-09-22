#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
class Collider;
class TransformComponent;

/// @brief 剛体の種別
enum class BodyType : int {
    Dynamic,    ///< 重力と力を受けて動く
    Kinematic,  ///< 速度を指定して動かし、他からは押されない
    Static,     ///< 動かない
    Count,
};

/// @brief 物理で動かす剛体
/// @details 速度と力を持ち、`PhysicsWorld` が固定ステップで積分する。
class RigidbodyComponent : public IComponent {
public:
    /// @brief 種別の名前（`BodyType` の並び）
    static constexpr const char* kBodyTypeNames[] = { "動的", "キネマティック", "静的" };

    /// @brief 受け付ける質量の下限（kg）
    static constexpr float kMinimumMass = 0.001f;

    const char* GetTypeName() const override { return "Rigidbody"; }

    REFLECT_BEGIN(RigidbodyComponent, "剛体")
        REFLECT_ENUM_ACCESSOR("bodyType", "種別", GetBodyType, SetBodyType, kBodyTypeNames,
            p.tooltip = "動的 = 重力と力で動く / キネマティック = 速度を指定して動かす / 静的 = 動かない")
        REFLECT_PROPERTY(mass_, "質量", p.range = Range(kMinimumMass, 10000.0f, 0.1f),
            p.tooltip = "kg。キネマティックと静的では無限として扱う")
        REFLECT_PROPERTY(useGravity_, "重力を受ける")
        REFLECT_PROPERTY(linearDamping_, "抗力", p.range = Range(0.0f, 10.0f, 0.01f),
            p.tooltip = "大きいほど速度が早く落ちる（0 で減らさない）")
        REFLECT_PROPERTY(freezeRotation_, "回転を止める",
            p.tooltip = "接触で回らなくなる（向きは自分で指定する）")
        REFLECT_ACCESSOR("velocity", "速度", GetVelocity, SetVelocity,
            p.range = Speed(0.1f),
            p.flags = ::CoreEngine::Reflection::PropertyFlags::NoSave,
            p.tooltip = "m/s。実行中の値なので保存しない")
        REFLECT_ACCESSOR("angularVelocity", "角速度", GetAngularVelocity, SetAngularVelocity,
            p.range = Speed(0.1f),
            p.flags = ::CoreEngine::Reflection::PropertyFlags::NoSave,
            p.tooltip = "rad/s。実行中の値なので保存しない")
        REFLECT_METHOD("AddForce", "力を加える", AddForce)
        REFLECT_METHOD("AddImpulse", "撃力を加える", AddImpulse)
        REFLECT_METHOD("AddTorque", "トルクを加える", AddTorque)
    REFLECT_END()

    /// @brief トランスフォームを使う
    bool RequiresComponent(const IComponent& other) const override;

    /// @brief 兄弟のトランスフォームを控える
    void Start() override;

    // ===== 力 =====

    /// @brief 力を加える（N。次のステップで速度へ変わる）
    /// @note 溜めた力はステップのたびに捨てるので、加え続けるには毎ステップ呼ぶ。
    void AddForce(const Vector3& force);

    /// @brief 撃力を加える（N・s。その場で速度が変わる）
    void AddImpulse(const Vector3& impulse);

    /// @brief トルクを加える（N・m。次のステップで角速度へ変わる）
    void AddTorque(const Vector3& torque);

    /// @brief ワールド上の点へ撃力を加える（その場で速度と角速度が変わる）
    void AddImpulseAtPoint(const Vector3& impulse, const Vector3& worldPoint);

    // ===== 状態 =====

    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& velocity) { velocity_ = velocity; }

    Vector3 GetAngularVelocity() const { return angularVelocity_; }
    void SetAngularVelocity(const Vector3& angularVelocity) { angularVelocity_ = angularVelocity; }

    /// @brief 回転を止めているか
    bool IsRotationFrozen() const { return freezeRotation_; }
    void SetRotationFrozen(bool frozen) { freezeRotation_ = frozen; }

    BodyType GetBodyType() const { return bodyType_; }
    void SetBodyType(BodyType type) { bodyType_ = type; }

    float GetMass() const { return mass_; }

    /// @brief 質量を設定する（下限 kMinimumMass で丸める）
    void SetMass(float mass);

    /// @brief 質量の逆数（動的でなければ 0 ＝ 無限質量）
    float GetInverseMass() const;

    /// @brief 重力と力を受けて動く剛体か
    bool IsDynamic() const { return bodyType_ == BodyType::Dynamic; }

    /// @brief ワールド空間の点が持つ速度（並進 ＋ 回転の寄与）
    Vector3 GetVelocityAtPoint(const Vector3& worldPoint) const;

    /// @brief 慣性の逆をワールド空間のベクトルへ掛ける（回りにくさの向きを持つ）
    /// @note 対角のローカル慣性を、今の向きの軸で挟んで掛ける。
    Vector3 ApplyInverseInertia(const Vector3& worldVector) const;

    /// @brief 質量と形とスケールから慣性を計算し直す
    /// @note 兄弟のコライダーを見る。コライダーが無ければ半径 0.5 の球として扱う。
    void RefreshInertia();

    // ===== 物理ステップ（PhysicsWorld が呼ぶ） =====

    /// @brief 重力と溜まった力を速度へ積み、抗力を掛ける
    void IntegrateVelocity(const Vector3& gravity, float deltaTime);

    /// @brief 速度の分だけ位置を進める
    void IntegratePosition(float deltaTime);

    /// @brief ワールド空間で位置をずらす（めり込みの押し戻し）
    void ApplyPositionDelta(const Vector3& delta);

    /// @brief 溜まったトルクを角速度へ積む
    void IntegrateAngularVelocity(float deltaTime);

    /// @brief 角速度の分だけ向きを回す
    void IntegrateRotation(float deltaTime);

private:
    /// @brief 兄弟のトランスフォームを引く（控えが無ければ引き直す）
    TransformComponent* FindTransform();

    /// @brief 慣性の計算に使う先頭のコライダー（無ければ nullptr）
    const Collider* GetColliderShape() const;

    BodyType bodyType_ = BodyType::Dynamic;
    float    mass_ = 1.0f;
    bool     useGravity_ = true;
    float    linearDamping_ = 0.0f;

    bool freezeRotation_ = false;

    Vector3 velocity_{};
    Vector3 accumulatedForce_{};
    Vector3 angularVelocity_{};
    Vector3 accumulatedTorque_{};

    /// ローカル軸に沿った慣性の逆（0 の成分はその軸で回らない）
    Vector3 localInverseInertia_{};

    TransformComponent* transform_ = nullptr;
};
}

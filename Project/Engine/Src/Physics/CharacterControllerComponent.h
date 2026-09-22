#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Geometry/Shapes.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
class Collider;
class ColliderComponent;
class CollisionWorld;
class TransformComponent;

/// @brief 剛体を使わないキャラクタの移動
/// @details 兄弟のコライダーの形で当たりを取り、壁に沿って滑り、低い段差を越え、
///          登れない角度の坂では滑り落ちる。押されたり倒れたりはしない。
class CharacterControllerComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "CharacterController"; }

    REFLECT_BEGIN(CharacterControllerComponent, "キャラクタコントローラ")
        REFLECT_PROPERTY(slopeLimit_, "登れる坂", p.range = Range(0.0f, 89.0f, 1.0f),
            p.tooltip = "度。これより急な坂では滑り落ちる")
        REFLECT_PROPERTY(stepOffset_, "越えられる段差", p.range = Range(0.0f, 2.0f, 0.01f),
            p.tooltip = "m。これ以下の段差は歩いたまま上がる")
        REFLECT_PROPERTY(skinWidth_, "表面の余白", p.range = Range(0.001f, 0.2f, 0.001f),
            p.tooltip = "m。壁との間にこれだけ隙間を残す")
        REFLECT_PROPERTY(useGravity_, "重力を受ける",
            p.tooltip = "接地していないと落ちる")
        REFLECT_READONLY_ACCESSOR("grounded", "接地している", IsGrounded)
        REFLECT_ACCESSOR("velocity", "速度", GetVelocity, SetVelocity,
            p.range = Speed(0.1f),
            p.flags = ::CoreEngine::Reflection::PropertyFlags::NoSave,
            p.tooltip = "m/s。実行中の値なので保存しない")
        REFLECT_METHOD("Move", "動かす", Move)
        REFLECT_METHOD("SimpleMove", "歩かせる", SimpleMove)
        REFLECT_METHOD("Jump", "跳ぶ", Jump)
    REFLECT_END()

    /// @brief トランスフォームとコライダーを使う
    bool RequiresComponent(const IComponent& other) const override;

    /// @brief 兄弟のコンポーネントを控える
    void Start() override;

    // ===== 移動 =====

    /// @brief 指定した量だけ動かす（m）
    /// @details 壁に当たれば沿って滑り、段差は上がり、急な坂では滑る。重力は含まない。
    void Move(const Vector3& motion);

    /// @brief 速度を指定して歩かせる（m/s）
    /// @details 上下成分は無視し、重力と落下を内部で足す。歩くだけならこれ 1 本で足りる。
    void SimpleMove(const Vector3& velocity);

    /// @brief 上向きに跳ぶ（m/s。接地していないときは何もしない）
    void Jump(float speed);

    // ===== 状態 =====

    /// @brief 登れる角度の床に乗っているか
    bool IsGrounded() const { return grounded_; }

    /// @brief 乗っている面の法線（接地していなければ真上）
    Vector3 GetGroundNormal() const { return groundNormal_; }

    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& velocity) { velocity_ = velocity; }

    float GetSlopeLimit() const { return slopeLimit_; }
    void SetSlopeLimit(float degrees) { slopeLimit_ = degrees; }

    float GetStepOffset() const { return stepOffset_; }
    void SetStepOffset(float offset) { stepOffset_ = offset; }

    // ===== シーンとの接続（PhysicsFeature が毎フレーム呼ぶ） =====

    /// @brief 問い合わせ先の衝突ワールドを差し込む
    void SetCollisionWorld(CollisionWorld* world) { world_ = world; }

    /// @brief 落下に使う重力を差し込む（シーンの設定に合わせる）
    void SetGravity(const Vector3& gravity) { gravity_ = gravity; }

private:
    /// @brief 1 回ぶんの移動を解く（Move が細かく割って呼ぶ）
    void MoveSlice(const Vector3& motion);

    /// @brief 判定に使うワールド空間のカプセル（兄弟のコライダーの形）
    Geometry::Capsule BuildCapsule() const;

    /// @brief 重なりを押し戻し、当たった面で速度を滑らせる
    /// @return 1 つでも押し戻したか
    bool ResolveOverlaps();

    /// @brief 動かしてから重なりを解く
    void MoveAndSlide(const Vector3& delta);

    /// @brief 進めなかったときだけ、体ごと持ち上げて段差に乗ることを試す
    void MoveWithStep(const Vector3& horizontal);

    /// @brief 足元を調べて接地の状態を更新する
    void UpdateGroundState();

    /// @brief 下ろして床を探す（見つからなければ元の高さへ戻す）
    /// @return 床に乗れたか
    bool SnapToGround(float distance);

    /// @brief ワールド空間で位置をずらす
    void Translate(const Vector3& delta);

    /// @brief 今のワールド位置
    Vector3 CurrentPosition() const;

    /// @brief 自分のコライダーか
    bool IsOwnCollider(const Collider* collider) const;

    /// @brief 兄弟のトランスフォームを引く（控えが無ければ引き直す）
    TransformComponent* FindTransform();

    float slopeLimit_ = 45.0f;
    float stepOffset_ = 0.3f;
    float skinWidth_ = 0.02f;
    bool  useGravity_ = true;

    Vector3 velocity_{};
    Vector3 gravity_{ 0.0f, -9.81f, 0.0f };
    Vector3 groundNormal_{ 0.0f, 1.0f, 0.0f };
    bool    grounded_ = false;

    CollisionWorld*     world_ = nullptr;
    TransformComponent* transform_ = nullptr;
    ColliderComponent*  colliders_ = nullptr;
};
}

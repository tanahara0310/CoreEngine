#include "pch.h"
#include "CharacterControllerComponent.h"

#include "Collision/ColliderComponent.h"
#include "Collision/CollisionWorld.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Math/Geometry/Intersect.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

REFLECT_REGISTER(CoreEngine::CharacterControllerComponent)
COMPONENT_REGISTER(CoreEngine::CharacterControllerComponent)

namespace CoreEngine
{
namespace {
    /// @brief 押し戻しをやり直す回数（角に挟まれたときに要る）
    constexpr int kResolveIterations = 4;

    /// @brief 接地を調べるために足元へ伸ばす長さ（m）
    constexpr float kGroundProbe = 0.05f;

    /// @brief 段差を下りたときに床を探しに行く距離の余裕（m）
    constexpr float kSnapMargin = 0.05f;

    /// @brief 落下速度の下限（m/s）
    constexpr float kMaxFallSpeed = -55.0f;

    /// @brief 接地しているときに床へ押し付けておく速さ（m/s）
    constexpr float kGroundStickSpeed = -2.0f;

    /// @brief 要求のこの割合すら進めなかったら、段差に乗ることを試す
    constexpr float kStepRetryRatio = 0.9f;

    /// @brief 段差の高さを測るときに見逃す誤差（m）
    constexpr float kStepTolerance = 0.02f;

    /// @brief 段差に乗れたと認めるために、余分に進めていてほしい距離（m）
    constexpr float kStepProgressEpsilon = 0.001f;

    /// @brief 1 回で動いてよい距離を、太さのこの割合までに抑える
    constexpr float kSubStepRatio = 0.5f;

    /// @brief 割り方の下限（m）。これより細かくは割らない
    constexpr float kMinimumSubStep = 0.05f;

    /// @brief 1 フレームで割ってよい回数の上限
    constexpr int kMaxSubSteps = 8;

    const Vector3 kUp{ 0.0f, 1.0f, 0.0f };

    /// @brief カプセルをすっぽり囲う箱（相手を絞り込むために少し広くする）
    Geometry::AABB QueryBounds(const Geometry::Capsule& capsule, float margin)
    {
        const Vector3 center = (capsule.start + capsule.end) * 0.5f;
        const float reach = Length(capsule.end - capsule.start) * 0.5f + capsule.radius + margin;
        const Vector3 extent{ reach, reach, reach };
        return Geometry::AABB{ center - extent, center + extent };
    }

    /// @brief カプセルと相手のコライダーの交差（法線はカプセル → 相手）
    bool IntersectWithCollider(const Geometry::Capsule& capsule, const Collider& other,
                               Geometry::Contact& outContact)
    {
        switch (other.GetShapeType()) {
        case ColliderShapeType::Sphere:
            return Geometry::Intersect(capsule, other.GetWorldSphere(), &outContact);
        case ColliderShapeType::Capsule:
            return Geometry::Intersect(capsule, other.GetWorldCapsule(), &outContact);
        default:
            return Geometry::Intersect(capsule, other.GetWorldOBB(), &outContact);
        }
    }
}

    bool CharacterControllerComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const TransformComponent*>(&other) != nullptr
            || dynamic_cast<const ColliderComponent*>(&other) != nullptr;
    }

    void CharacterControllerComponent::Start()
    {
        transform_ = Sibling<TransformComponent>();
        colliders_ = Sibling<ColliderComponent>();
    }

    //================================================
    // 移動
    //================================================

    void CharacterControllerComponent::Move(const Vector3& motion)
    {
        if (!FindTransform() || !world_) {
            return;
        }
        if (!colliders_) {
            colliders_ = Sibling<ColliderComponent>();
        }

        // 1 回の移動が太さを超えると薄いものを飛び越えるので、割って進める
        // （フレームが重くて一度に大きく動く場面で効く）
        const float distance = Length(motion);
        const float limit = (std::max)(BuildCapsule().radius * kSubStepRatio, kMinimumSubStep);

        int steps = 1;
        if (distance > limit) {
            steps = (std::min)(static_cast<int>(distance / limit) + 1, kMaxSubSteps);
        }

        const Vector3 slice = motion * (1.0f / static_cast<float>(steps));
        for (int i = 0; i < steps; ++i) {
            MoveSlice(slice);
        }
    }

    void CharacterControllerComponent::MoveSlice(const Vector3& motion)
    {
        const bool wasGrounded = grounded_;

        // 上下と水平を分けて解く
        const Vector3 vertical{ 0.0f, motion.y, 0.0f };
        const Vector3 horizontal{ motion.x, 0.0f, motion.z };

        if (std::abs(vertical.y) > 0.0f) {
            MoveAndSlide(vertical);
        }
        if (Dot(horizontal, horizontal) > 0.0f) {
            if (wasGrounded && stepOffset_ > 0.0f) {
                MoveWithStep(horizontal);
            } else {
                MoveAndSlide(horizontal);
            }
        }

        // 坂や段差を下るときに浮かないよう、床があれば下ろす
        if (wasGrounded && velocity_.y <= 0.0f) {
            SnapToGround(stepOffset_ + kSnapMargin);
        }

        UpdateGroundState();
    }

    void CharacterControllerComponent::SimpleMove(const Vector3& velocity)
    {
        const float deltaTime = Time::DeltaTime();
        if (deltaTime <= 0.0f) {
            return;
        }

        // 落下は自分で積む。接地している間は軽く押し付けて段差から浮かせない
        if (useGravity_) {
            if (grounded_ && velocity_.y <= 0.0f) {
                velocity_.y = kGroundStickSpeed;
            } else {
                velocity_.y += gravity_.y * deltaTime;
                velocity_.y = (std::max)(velocity_.y, kMaxFallSpeed);
            }
        } else {
            velocity_.y = 0.0f;
        }

        velocity_.x = velocity.x;
        velocity_.z = velocity.z;

        Move(velocity_ * deltaTime);
    }

    void CharacterControllerComponent::Jump(float speed)
    {
        if (!grounded_) {
            return;
        }
        velocity_.y = speed;
        grounded_ = false;
    }

    //================================================
    // 当たりの解決
    //================================================

    void CharacterControllerComponent::MoveAndSlide(const Vector3& delta)
    {
        Translate(delta);

        for (int i = 0; i < kResolveIterations; ++i) {
            if (!ResolveOverlaps()) {
                break;
            }
        }
    }

    void CharacterControllerComponent::MoveWithStep(const Vector3& horizontal)
    {
        // まずそのまま動いてみる
        const Vector3 before = CurrentPosition();
        const Vector3 enteringVelocity = velocity_;
        MoveAndSlide(horizontal);

        const Vector3 plain = CurrentPosition();
        const Vector3 plainVelocity = velocity_;

        const Vector3 gained{ plain.x - before.x, 0.0f, plain.z - before.z };
        const float wanted = Length(horizontal);
        if (Length(gained) >= wanted * kStepRetryRatio) {
            return;   // ぶつからずに進めたので段差は要らない
        }

        // 体ごと段差のぶん持ち上げて、同じ移動をやり直す
        Translate(before - plain);
        velocity_ = enteringVelocity;
        Translate(Vector3{ 0.0f, stepOffset_, 0.0f });
        MoveAndSlide(horizontal);

        // 乗れたなら床まで下ろす。床が無ければ持ち上げなかった結果を使う
        bool climbed = SnapToGround(stepOffset_ + kSnapMargin);

        const Vector3 stepped = CurrentPosition();
        const Vector3 steppedGain{ stepped.x - before.x, 0.0f, stepped.z - before.z };

        // 持ち上げても前へ進めていないなら、段差ではなく壁。
        // カプセルの下は丸いので、そのままだと角へ少しずつ乗り上げて登ってしまう。
        if (climbed && Length(steppedGain) <= Length(gained) + kStepProgressEpsilon) {
            climbed = false;
        }

        // 許した段差より高いところへ乗れてしまった場合も無かったことにする
        if (climbed && (stepped.y - before.y) > stepOffset_ + kStepTolerance) {
            climbed = false;
        }

        if (!climbed) {
            Translate(plain - CurrentPosition());
            velocity_ = plainVelocity;
        }
    }

    bool CharacterControllerComponent::ResolveOverlaps()
    {
        Geometry::Capsule capsule = BuildCapsule();
        if (capsule.radius <= 0.0f) {
            return false;
        }

        const uint64_t layerMask = colliders_ && colliders_->GetFirst()
            ? world_->GetLayerMask(colliders_->GetFirst()->GetLayer())
            : CollisionWorld::kAllLayers;

        std::vector<Collider*> candidates;
        world_->OverlapBox(QueryBounds(capsule, skinWidth_), layerMask, candidates);

        bool pushed = false;

        for (Collider* const other : candidates) {
            if (!other || other->IsTrigger() || IsOwnCollider(other)) { continue; }

            Geometry::Contact contact;
            if (!IntersectWithCollider(capsule, *other, contact)) { continue; }

            // contact.normal はこちらから相手へ向くので、逆が押し戻す向き
            const Vector3 escape = contact.normal * -1.0f;
            const float distance = contact.depth + skinWidth_;
            if (distance <= 0.0f) { continue; }

            Translate(escape * distance);
            capsule.start += escape * distance;
            capsule.end += escape * distance;
            pushed = true;

            // 面へ食い込む向きの速度を落とす（壁に沿って滑る）
            const float into = Dot(velocity_, escape);
            if (into < 0.0f) {
                velocity_ -= escape * into;
            }
        }

        return pushed;
    }

    void CharacterControllerComponent::UpdateGroundState()
    {
        grounded_ = false;
        groundNormal_ = kUp;

        if (!world_) { return; }

        Geometry::Capsule capsule = BuildCapsule();
        if (capsule.radius <= 0.0f) { return; }

        // 少し沈めたカプセルで調べる（ぴったり乗っていても床を見つけられる）
        capsule.start.y -= kGroundProbe;
        capsule.end.y -= kGroundProbe;

        const uint64_t layerMask = colliders_ && colliders_->GetFirst()
            ? world_->GetLayerMask(colliders_->GetFirst()->GetLayer())
            : CollisionWorld::kAllLayers;

        std::vector<Collider*> candidates;
        world_->OverlapBox(QueryBounds(capsule, kGroundProbe), layerMask, candidates);

        const float limit = std::cos(slopeLimit_ * std::numbers::pi_v<float> / 180.0f);
        float bestSlope = -1.0f;

        for (Collider* const other : candidates) {
            if (!other || other->IsTrigger() || IsOwnCollider(other)) { continue; }

            Geometry::Contact contact;
            if (!IntersectWithCollider(capsule, *other, contact)) { continue; }

            // 押し戻す向きが上を向いているほど、床として平ら
            const Vector3 escape = contact.normal * -1.0f;
            const float slope = Dot(escape, kUp);
            if (slope > bestSlope) {
                bestSlope = slope;
                groundNormal_ = escape;
            }
        }

        grounded_ = (bestSlope >= limit);

        // 登れない坂では法線を残したまま接地扱いにしない（重力で滑り落ちる）
        if (!grounded_ && bestSlope <= 0.0f) {
            groundNormal_ = kUp;
        }
    }

    bool CharacterControllerComponent::SnapToGround(float distance)
    {
        if (distance <= 0.0f) { return false; }

        const Vector3 before = CurrentPosition();

        // 下ろしてみて、床に当たれば押し戻しがその場で受け止める
        Translate(Vector3{ 0.0f, -distance, 0.0f });
        for (int i = 0; i < kResolveIterations; ++i) {
            if (!ResolveOverlaps()) { break; }
        }

        UpdateGroundState();
        if (!grounded_) {
            // 下には乗れる床が無かったので元へ戻す
            Translate(before - CurrentPosition());
            return false;
        }
        return true;
    }

    //================================================
    // 補助
    //================================================

    Geometry::Capsule CharacterControllerComponent::BuildCapsule() const
    {
        const Collider* const collider = colliders_ ? colliders_->GetFirst() : nullptr;
        if (!collider) {
            return Geometry::Capsule{};
        }
        return collider->GetWorldCapsule();
    }

    void CharacterControllerComponent::Translate(const Vector3& delta)
    {
        if (transform_) {
            transform_->ApplyWorldDelta(delta);
        }
    }

    Vector3 CharacterControllerComponent::CurrentPosition() const
    {
        return GetOwner() ? GetOwner()->GetWorldPosition() : Vector3{};
    }

    bool CharacterControllerComponent::IsOwnCollider(const Collider* collider) const
    {
        return collider && GetOwner() && collider->GetOwner() == GetOwner();
    }

    TransformComponent* CharacterControllerComponent::FindTransform()
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }
}

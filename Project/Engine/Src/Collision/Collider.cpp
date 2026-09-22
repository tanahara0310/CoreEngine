#include "pch.h"
#include "Collider.h"
#include "GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
namespace {

    //================================================
    // 形状ペアごとの判定ディスパッチ
    //================================================
    /// 実装は Math/Geometry 側の 1 箇所にあり、ここはワールド形状を作って渡すだけ。
    /// 表の形にしてあるので、形状を増やすと次元が合わずコンパイルエラーで埋め忘れに気づける。

    using IntersectFn = bool(*)(const Collider&, const Collider&, Geometry::Contact*);

    bool SphereVsSphere(const Collider& a, const Collider& b, Geometry::Contact* c) {
        return Geometry::Intersect(a.GetWorldSphere(), b.GetWorldSphere(), c);
    }
    bool SphereVsBox(const Collider& a, const Collider& b, Geometry::Contact* c) {
        return Geometry::Intersect(a.GetWorldSphere(), b.GetWorldOBB(), c);
    }
    bool BoxVsSphere(const Collider& a, const Collider& b, Geometry::Contact* c) {
        return Geometry::Intersect(a.GetWorldOBB(), b.GetWorldSphere(), c);
    }
    bool BoxVsBox(const Collider& a, const Collider& b, Geometry::Contact* c) {
        return Geometry::Intersect(a.GetWorldOBB(), b.GetWorldOBB(), c);
    }

    constexpr int kShapeCount = static_cast<int>(ColliderShapeType::Count);

    constexpr IntersectFn kDispatch[kShapeCount][kShapeCount] = {
        /*            相手: Sphere      Box        */
        /* Sphere */ { SphereVsSphere, SphereVsBox },
        /* Box    */ { BoxVsSphere,    BoxVsBox    },
    };
}

namespace {
    /// @brief 一意 ID の発番。0 は「無効」に予約するので 1 から始める。
    /// @note 再利用しないので ABA が原理的に起きない。64bit なので枯渇は現実的に無い。
    uint64_t NextColliderId()
    {
        static uint64_t counter = 0;
        return ++counter;
    }
}

Collider::Collider()
    : id_(NextColliderId())
{
}

Collider::Collider(GameObject* owner, const CollisionShape& shape, CollisionLayer layer)
    : id_(NextColliderId())
    , shape_(shape)
    , owner_(owner)
    , layer_(layer)
{
}

//================================================
// 判定
//================================================

bool Collider::Intersects(const Collider& other, Geometry::Contact* outContact) const
{
    const int a = static_cast<int>(shape_.type);
    const int b = static_cast<int>(other.shape_.type);
    if (a < 0 || a >= kShapeCount || b < 0 || b >= kShapeCount) {
        return false;
    }
    return kDispatch[a][b](*this, other, outContact);
}

//================================================
// ワールド空間の形状
//================================================

Vector3 Collider::GetWorldScale() const
{
    if (owner_ == nullptr) return { 1.0f, 1.0f, 1.0f };
    return owner_->GetWorldScale();
}

Vector3 Collider::GetWorldCenter() const
{
    if (owner_ == nullptr) return shape_.offset;

    const Vector3 scale = GetWorldScale();
    const Vector3 scaledOffset{
        shape_.offset.x * scale.x,
        shape_.offset.y * scale.y,
        shape_.offset.z * scale.z
    };
    // オフセットは持ち主の向きに合わせて回す
    Vector3 axisX, axisY, axisZ;
    owner_->GetWorldAxes(axisX, axisY, axisZ);
    const Vector3 rotatedOffset =
        axisX * scaledOffset.x + axisY * scaledOffset.y + axisZ * scaledOffset.z;

    return owner_->GetWorldPosition() + rotatedOffset;
}

Geometry::Sphere Collider::GetWorldSphere() const
{
    const Vector3 scale = GetWorldScale();
    const float maxScale = (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });

    if (shape_.type == ColliderShapeType::Box) {
        // ボックスの外接球（対角線の半分）。非等倍スケールでは球でいられないので
        // 安全側（大きい方）に倒す。
        const Vector3 half{
            shape_.size.x * std::abs(scale.x) * 0.5f,
            shape_.size.y * std::abs(scale.y) * 0.5f,
            shape_.size.z * std::abs(scale.z) * 0.5f
        };
        return Geometry::Sphere{ GetWorldCenter(), Length(half) };
    }

    return Geometry::Sphere{ GetWorldCenter(), shape_.radius * maxScale };
}

Geometry::AABB Collider::GetWorldAABB() const
{
    const Vector3 center = GetWorldCenter();
    const Vector3 scale = GetWorldScale();

    if (shape_.type == ColliderShapeType::Sphere) {
        const float maxScale = (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
        const float r = shape_.radius * maxScale;
        const Vector3 extent{ r, r, r };
        return Geometry::AABB{ center - extent, center + extent };
    }

    return GetWorldOBB().ToAABB();
}

Geometry::OBB Collider::GetWorldOBB() const
{
    const Vector3 scale = GetWorldScale();

    Geometry::OBB obb;
    obb.center = GetWorldCenter();

    if (shape_.type == ColliderShapeType::Sphere) {
        // 球を箱として見るときは外接する立方体にする
        const float maxScale =
            (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
        const float radius = shape_.radius * maxScale;
        obb.halfExtents = Vector3{ radius, radius, radius };
        return obb;
    }

    obb.halfExtents = Vector3{
        shape_.size.x * std::abs(scale.x) * 0.5f,
        shape_.size.y * std::abs(scale.y) * 0.5f,
        shape_.size.z * std::abs(scale.z) * 0.5f
    };
    if (owner_) {
        owner_->GetWorldAxes(obb.axes[0], obb.axes[1], obb.axes[2]);
    }
    return obb;
}

//================================================
// 形状の変更
//================================================

void Collider::SetRadius(float radius)
{
    if (shape_.type == ColliderShapeType::Sphere) {
        shape_.radius = radius;
    }
}

void Collider::SetSize(const Vector3& size)
{
    if (shape_.type == ColliderShapeType::Box) {
        shape_.size = size;
    }
}

//================================================
// 衝突イベント
//================================================

namespace {
    /// @brief コールバックへ渡す接触情報を組み立てる
    CollisionInfo MakeInfo(Collider* self, Collider* other, const Geometry::Contact& contact)
    {
        CollisionInfo info;
        info.other         = other->GetOwner();
        info.selfCollider  = self;
        info.otherCollider = other;
        info.normal        = contact.normal;
        info.depth         = contact.depth;
        info.point         = contact.point;
        // 強さは両側の記録のうち大きい方（片方だけが剛体のこともある）
        info.impulse       = (std::max)(self->GetLastImpulse(), other->GetLastImpulse());
        return info;
    }
}

void Collider::OnCollisionEnter(Collider* other, const Geometry::Contact& contact) {
    if (owner_ && other && other->owner_) {
        owner_->NotifyCollisionEnter(MakeInfo(this, other, contact));
    }
}

void Collider::OnCollisionStay(Collider* other, const Geometry::Contact& contact) {
    if (owner_ && other && other->owner_) {
        owner_->NotifyCollisionStay(MakeInfo(this, other, contact));
    }
}

void Collider::OnCollisionExit(Collider* other) {
    if (owner_ && other && other->owner_) {
        // 接触は既に切れているので normal / depth は持たない
        owner_->NotifyCollisionExit(MakeInfo(this, other, Geometry::Contact{}));
    }
}
}

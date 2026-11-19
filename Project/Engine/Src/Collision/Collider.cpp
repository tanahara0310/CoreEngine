#include "pch.h"
#include "Collider.h"
#include "GameObject/GameObject.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#include "WorldTransform/WorldTransform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace CoreEngine
{
namespace {
    constexpr int kShapeCount = static_cast<int>(ColliderShapeType::Count);

    CVar<float> cvMeshFlatEdgeAngle{
        "sys.Collision.MeshFlatEdgeAngle", 1.0f,
        "メッシュの縁を平らとみなす角度（度）。これ以下の折れ目では、継ぎ目に引っかからないよう面の向きで押す",
        CVarRange{ 0.0f, 30.0f } };

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
    return VisitWorldShape(*this, [&](const auto& selfShape) {
        return VisitWorldShape(other, [&](const auto& otherShape) {
            return Geometry::Intersect(selfShape, otherShape, outContact);
        });
    });
}

//================================================
// ワールド空間の形状
//================================================

const Collider::WorldFrame& Collider::GetWorldFrame() const
{
    if (owner_ == nullptr) {
        frame_ = WorldFrame{};
        frameCached_ = false;
        boundsValid_ = false;
        return frame_;
    }

    const Matrix4x4* const world = owner_->FindWorldMatrix();
    if (world == nullptr) {
        // 行列を持たない持ち主は控えずに毎回取る
        frame_.position = owner_->GetWorldPosition();
        frame_.scale = owner_->GetWorldScale();
        owner_->GetWorldAxes(frame_.axes[0], frame_.axes[1], frame_.axes[2]);
        frameCached_ = false;
        boundsValid_ = false;
        return frame_;
    }

    if (frameCached_ && std::memcmp(&frameMatrix_, world, sizeof(Matrix4x4)) == 0) {
        return frame_;
    }

    frameMatrix_ = *world;
    frame_.position = { world->m[3][0], world->m[3][1], world->m[3][2] };
    frame_.scale = ScaleOfWorldMatrix(*world);
    AxesOfWorldMatrix(*world, frame_.axes[0], frame_.axes[1], frame_.axes[2]);
    frameCached_ = true;
    boundsValid_ = false;
    return frame_;
}

Vector3 Collider::WorldCenterIn(const WorldFrame& frame) const
{
    if (owner_ == nullptr) return shape_.offset;

    const Vector3& scale = frame.scale;
    const Vector3 scaledOffset{
        shape_.offset.x * scale.x,
        shape_.offset.y * scale.y,
        shape_.offset.z * scale.z
    };
    // オフセットは持ち主の向きに合わせて回す
    const Vector3 rotatedOffset =
        frame.axes[0] * scaledOffset.x + frame.axes[1] * scaledOffset.y + frame.axes[2] * scaledOffset.z;

    return frame.position + rotatedOffset;
}

Vector3 Collider::GetWorldScale() const
{
    return GetWorldFrame().scale;
}

Vector3 Collider::GetWorldCenter() const
{
    return WorldCenterIn(GetWorldFrame());
}

float MaxAbsScale(const Vector3& scale)
{
    return (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
}

float WorldCapsuleRadius(const CollisionShape& shape, const Vector3& scale)
{
    return shape.radius * (std::max)(std::abs(scale.x), std::abs(scale.z));
}

namespace {
    /// @brief カプセルの半径と、円筒部分の半分の長さをワールド寸法で出す
    /// @note 太さは横方向のスケール、長さは上方向のスケールで伸ばす。
    void WorldCapsuleMetrics(const CollisionShape& shape, const Vector3& scale,
                             float& outRadius, float& outHalfLength)
    {
        outRadius = WorldCapsuleRadius(shape, scale);
        outHalfLength = shape.CylinderLength() * std::abs(scale.y) * 0.5f;
    }
}

Geometry::Sphere Collider::GetWorldSphere() const
{
    const WorldFrame& frame = GetWorldFrame();
    const Vector3 scale = frame.scale;
    const float maxScale = MaxAbsScale(scale);

    if (shape_.type == ColliderShapeType::Mesh) {
        const Geometry::OBB box = GetWorldOBB();
        return Geometry::Sphere{ box.center, Length(box.halfExtents) };
    }

    if (shape_.type == ColliderShapeType::Capsule) {
        float radius = 0.0f, halfLength = 0.0f;
        WorldCapsuleMetrics(shape_, scale, radius, halfLength);
        return Geometry::Sphere{ WorldCenterIn(frame), halfLength + radius };
    }

    if (shape_.type == ColliderShapeType::Box) {
        // ボックスの外接球（対角線の半分）。非等倍スケールでは球でいられないので
        // 安全側（大きい方）に倒す。
        const Vector3 half{
            shape_.size.x * std::abs(scale.x) * 0.5f,
            shape_.size.y * std::abs(scale.y) * 0.5f,
            shape_.size.z * std::abs(scale.z) * 0.5f
        };
        return Geometry::Sphere{ WorldCenterIn(frame), Length(half) };
    }

    return Geometry::Sphere{ WorldCenterIn(frame), shape_.radius * maxScale };
}

Geometry::AABB Collider::GetWorldAABB() const
{
    const WorldFrame& frame = GetWorldFrame();
    if (boundsValid_) {
        return bounds_;
    }

    Geometry::AABB bounds;
    if (shape_.type == ColliderShapeType::Sphere) {
        const Vector3 center = WorldCenterIn(frame);
        const float r = shape_.radius * MaxAbsScale(frame.scale);
        const Vector3 extent{ r, r, r };
        bounds = Geometry::AABB{ center - extent, center + extent };
    }
    else {
        bounds = GetWorldOBB().ToAABB();
    }

    // 持ち主の行列から出した形だけを控える（行列の無い持ち主は毎回出し直す）
    if (frameCached_) {
        bounds_ = bounds;
        boundsValid_ = true;
    }
    return bounds;
}

Geometry::OBB Collider::GetWorldOBB() const
{
    const WorldFrame& frame = GetWorldFrame();
    const Vector3 scale = frame.scale;

    Geometry::OBB obb;
    obb.center = WorldCenterIn(frame);

    if (shape_.type == ColliderShapeType::Sphere) {
        // 球を箱として見るときは外接する立方体にする
        const float radius = shape_.radius * MaxAbsScale(scale);
        obb.halfExtents = Vector3{ radius, radius, radius };
        return obb;
    }

    obb.axes[0] = frame.axes[0];
    obb.axes[1] = frame.axes[1];
    obb.axes[2] = frame.axes[2];

    if (shape_.type == ColliderShapeType::Capsule) {
        float radius = 0.0f, halfLength = 0.0f;
        WorldCapsuleMetrics(shape_, scale, radius, halfLength);
        obb.halfExtents = Vector3{ radius, halfLength + radius, radius };
        return obb;
    }

    if (shape_.type == ColliderShapeType::Mesh) {
        // 三角形を包むローカルの箱を、持ち主の向きと拡大縮小で置く（三角形がまだ無ければ大きさ 0）
        if (mesh_) {
            const Geometry::AABB& local = mesh_->Bounds();
            const Vector3 half = local.GetSize() * 0.5f;
            obb.center = GetWorldMeshView().ToWorld(local.GetCenter());
            obb.halfExtents = Vector3{ half.x * std::abs(scale.x), half.y * std::abs(scale.y), half.z * std::abs(scale.z) };
        }
        return obb;
    }

    obb.halfExtents = Vector3{
        shape_.size.x * std::abs(scale.x) * 0.5f,
        shape_.size.y * std::abs(scale.y) * 0.5f,
        shape_.size.z * std::abs(scale.z) * 0.5f
    };
    return obb;
}

Geometry::MeshView Collider::GetWorldMeshView() const
{
    Geometry::MeshView view;
    if (shape_.type != ColliderShapeType::Mesh) {
        return view;
    }
    const WorldFrame& frame = GetWorldFrame();
    view.mesh = mesh_.get();
    view.origin = WorldCenterIn(frame);
    view.scale = frame.scale;
    view.axes[0] = frame.axes[0];
    view.axes[1] = frame.axes[1];
    view.axes[2] = frame.axes[2];
    view.oneSided = shape_.meshOneSided;
    view.flatEdgeAngle = cvMeshFlatEdgeAngle.Get() * (std::numbers::pi_v<float> / 180.0f);
    return view;
}

Vector3 Collider::GetLocalBoxSize() const
{
    if (shape_.type == ColliderShapeType::Mesh) {
        return mesh_ ? mesh_->Bounds().GetSize() : Vector3{ 0.0f, 0.0f, 0.0f };
    }
    return shape_.size;
}

//================================================
// メッシュの形
//================================================

void Collider::RefreshMesh()
{
    const CollisionMeshStatus previous = meshStatus_;
    const Geometry::TriangleMesh* const previousMesh = mesh_.get();

    if (shape_.type != ColliderShapeType::Mesh || owner_ == nullptr) {
        if (previousMesh) {
            boundsValid_ = false;
        }
        mesh_.reset();
        meshKey_ = nullptr;
        meshLod_ = -1;
        meshStatus_ = CollisionMeshStatus::None;
        return;
    }

    const CollisionMeshSourceInfo source = CollisionMeshLibrary::Identify(*owner_, shape_);
    if (source.key == nullptr) {
        mesh_.reset();
        meshKey_ = nullptr;
        meshLod_ = -1;
        meshStatus_ = source.status;
    }
    else if (source.key != meshKey_ || shape_.meshLod != meshLod_ || !mesh_) {
        const CollisionMeshLibrary::Result result = CollisionMeshLibrary::Acquire(source.key, shape_.meshLod);
        mesh_ = result.mesh;
        meshKey_ = source.key;
        meshLod_ = shape_.meshLod;
        meshStatus_ = result.status;
    }

    if (mesh_.get() != previousMesh) {
        boundsValid_ = false;
    }

    // 使えない理由が新しく分かったときだけ知らせる
    if (meshStatus_ != previous
        && (meshStatus_ == CollisionMeshStatus::Skinned || meshStatus_ == CollisionMeshStatus::Empty)) {
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
            "「{}」のメッシュのコライダー: {}", owner_->GetName(), ToDisplayText(meshStatus_));
    }
}

Geometry::Capsule Collider::GetWorldCapsule() const
{
    const WorldFrame& frame = GetWorldFrame();
    const Vector3 center = WorldCenterIn(frame);

    // カプセル以外は、長さの無いカプセル（＝外接球）として扱う
    if (shape_.type != ColliderShapeType::Capsule) {
        const Geometry::Sphere sphere = GetWorldSphere();
        return Geometry::Capsule{ sphere.center, sphere.center, sphere.radius };
    }

    float radius = 0.0f, halfLength = 0.0f;
    WorldCapsuleMetrics(shape_, frame.scale, radius, halfLength);

    const Vector3 half = frame.axes[1] * halfLength;
    return Geometry::Capsule{ center - half, center + half, radius };
}

//================================================
// 形状の変更
//================================================

void Collider::SetRadius(float radius)
{
    if (shape_.type == ColliderShapeType::Sphere || shape_.type == ColliderShapeType::Capsule) {
        shape_.radius = radius;
        boundsValid_ = false;
    }
}

void Collider::SetSize(const Vector3& size)
{
    if (shape_.type == ColliderShapeType::Box) {
        shape_.size = size;
        boundsValid_ = false;
    }
}

void Collider::SetHeight(float height)
{
    if (shape_.type == ColliderShapeType::Capsule) {
        shape_.height = height;
        boundsValid_ = false;
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

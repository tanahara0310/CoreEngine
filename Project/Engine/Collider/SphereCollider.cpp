#include "SphereCollider.h"
#include "AABBCollider.h"
#include "Engine/Utility/Collision/CollisionUtils.h"


namespace CoreEngine
{
SphereCollider::SphereCollider(GameObject* owner, float r) {
   type_ = ColliderType::Sphere;
   owner_ = owner;
   radius_ = r;
}

bool SphereCollider::CheckCollision(Collider* other) const {
   if (!other) return false;

   if (other->GetType() == ColliderType::Sphere) {
      const SphereCollider& s = static_cast<const SphereCollider&>(*other);
      CollisionUtils::Sphere sphere1 = { GetPosition(), radius_ };
      CollisionUtils::Sphere sphere2 = { s.GetPosition(), s.radius_ };
      return CollisionUtils::IsColliding(sphere1, sphere2);
   } else if (other->GetType() == ColliderType::AABB) {
      const AABBCollider& a = static_cast<const AABBCollider&>(*other);
      BoundingBox aabb = { a.GetMin(), a.GetMax() };
      CollisionUtils::Sphere sphere = { GetPosition(), radius_ };
      return CollisionUtils::IsColliding(sphere, aabb);
   }
   return false;
}

void SphereCollider::SetRadius(float radius) {
   radius_ = radius;
}
}

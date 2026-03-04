#include "AABBCollider.h"
#include "SphereCollider.h"
#include "Utility/Collision/CollisionUtils.h"

#ifdef _DEBUG
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Line/LineManager.h"
#include "Camera/ICamera.h"
#endif


namespace CoreEngine
{
AABBCollider::AABBCollider(GameObject* owner, const Vector3& size) {
   type_ = ColliderType::AABB;
   owner_ = owner;
   size_ = size;
}

bool AABBCollider::CheckCollision(Collider* other) const {
   if (!other) return false;

   if (other->GetType() == ColliderType::Sphere) {
      const SphereCollider& s = static_cast<const SphereCollider&>(*other);
      BoundingBox aabb = { GetMin(), GetMax() };
      CollisionUtils::Sphere sphere = { s.GetPosition(), s.GetRadius() };
      return CollisionUtils::IsColliding(sphere, aabb);
   } else if (other->GetType() == ColliderType::AABB) {
      const AABBCollider& a = static_cast<const AABBCollider&>(*other);
      BoundingBox aabb1 = { GetMin(), GetMax() };
      BoundingBox aabb2 = { a.GetMin(), a.GetMax() };
      return CollisionUtils::IsColliding(aabb1, aabb2);
   }
   return false;
}

void AABBCollider::SetSize(const Vector3& size) {
   size_ = size;
}


CoreEngine::Vector3 AABBCollider::GetMin() const {
   return GetPosition() - size_ * 0.5f;
}

CoreEngine::Vector3 AABBCollider::GetMax() const {
   return GetPosition() + size_ * 0.5f;
}

#ifdef _DEBUG
void AABBCollider::DrawDebug(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, const CoreEngine::Vector3& color) const {
   if (!pipeline || !camera) return;

   CoreEngine::Vector3 center = GetPosition();
   auto lines = CoreEngine::LineManager::GenerateBoxLines(center, size_, color, 1.0f);
   pipeline->AddLines(lines);
}
#endif
}

#include "Collider.h"
#include "Engine/ObjectCommon/GameObject.h"


namespace CoreEngine
{
Vector3 Collider::GetPosition() const {
   if (owner_ == nullptr) return Vector3();

   return owner_->GetWorldPosition();
}

ColliderType Collider::GetType() const {
   return type_;
}

void Collider::OnCollisionEnter(Collider* other) {
   if (owner_ && other && other->owner_) {
      owner_->OnCollisionEnter(other->owner_);
   }
}

void Collider::OnCollisionStay(Collider* other) {
   if (owner_ && other && other->owner_) {
      owner_->OnCollisionStay(other->owner_);
   }
}

void Collider::OnCollisionExit(Collider* other) {
   if (owner_ && other && other->owner_) {
      owner_->OnCollisionExit(other->owner_);
   }
}
}

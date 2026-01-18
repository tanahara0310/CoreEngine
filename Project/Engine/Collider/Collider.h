#pragma once
#include "CollisionLayer.h"
#include "MathCore.h"


namespace CoreEngine
{
enum class ColliderType {
   None,
   Sphere,
   AABB,
};

class GameObject;

class Collider {
public:
   virtual ~Collider() = default;

   virtual bool CheckCollision(Collider* other) const = 0;

   Vector3 GetPosition() const;
   ColliderType GetType() const;

   void OnCollisionEnter(Collider* other);
   void OnCollisionStay(Collider* other);
   void OnCollisionExit(Collider* other);

   void SetLayer(CollisionLayer layer) { layer_ = layer; }
   CollisionLayer GetLayer() const { return layer_; }
   
   void SetOwner(GameObject* owner) { owner_ = owner; }
   GameObject* GetOwner() const { return owner_; }

   void SetEnabled(bool enabled) { isEnabled_ = enabled; }
   bool IsEnabled() const { return isEnabled_; }

   virtual void SetSize(const Vector3& size) { (void)size; }

   virtual void SetRadius(float radius) { (void)radius; }
protected:
   ColliderType type_ = ColliderType::None;
   GameObject* owner_ = nullptr;
   CollisionLayer layer_ = CollisionLayer::Default;
   bool isEnabled_ = true;
};
}

#pragma once
#include "Collider.h"


namespace CoreEngine
{
class SphereCollider : public Collider {
public:
   SphereCollider(GameObject* owner, float r);

   bool CheckCollision(Collider* other) const override;

   void SetRadius(float radius) override;

   /// @brief 球の半径を取得
   float GetRadius() const { return radius_; }

private:
   float radius_{};
};
}

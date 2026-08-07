#include "pch.h"
#include "ColliderComponent.h"

#include <algorithm>

namespace CoreEngine
{
    Collider& ColliderComponent::Add(const CollisionShape& shape, CollisionLayer layer)
    {
        colliders_.push_back(std::make_unique<Collider>(owner_, shape, layer));
        return *colliders_.back();
    }

    Collider& ColliderComponent::AddSphere(float radius, CollisionLayer layer, const Vector3& offset)
    {
        return Add(CollisionShape::MakeSphere(radius, offset), layer);
    }

    Collider& ColliderComponent::AddBox(const Vector3& size, CollisionLayer layer, const Vector3& offset)
    {
        return Add(CollisionShape::MakeBox(size, offset), layer);
    }

    void ColliderComponent::RemoveAll()
    {
        // 即 delete すると、衝突判定ループが保持している生ポインタが宙に浮く
        // （OnCollisionEnter の中から呼ばれるのが典型）。フレーム末まで実体を残す。
        for (auto& collider : colliders_) {
            if (collider) {
                retired_.push_back(std::move(collider));
            }
        }
        colliders_.clear();
    }

    bool ColliderComponent::Remove(Collider* collider)
    {
        if (!collider) { return false; }

        auto it = std::find_if(colliders_.begin(), colliders_.end(),
            [collider](const std::unique_ptr<Collider>& entry) { return entry.get() == collider; });
        if (it == colliders_.end()) {
            return false;
        }

        retired_.push_back(std::move(*it));
        colliders_.erase(it);
        return true;
    }

    void ColliderComponent::ReleaseRetired()
    {
        retired_.clear();
    }

    Collider* ColliderComponent::GetFirst()
    {
        return colliders_.empty() ? nullptr : colliders_.front().get();
    }

    const Collider* ColliderComponent::GetFirst() const
    {
        return colliders_.empty() ? nullptr : colliders_.front().get();
    }

    Collider* ColliderComponent::Get(size_t index)
    {
        return (index < colliders_.size()) ? colliders_[index].get() : nullptr;
    }

    const Collider* ColliderComponent::Get(size_t index) const
    {
        return (index < colliders_.size()) ? colliders_[index].get() : nullptr;
    }
}

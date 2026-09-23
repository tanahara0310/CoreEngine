#include "pch.h"
#include "ColliderComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>
#include <string>

REFLECT_DEFINE_BEGIN(CoreEngine::ColliderComponent, "コライダー")
    REFLECT_PARTIAL()
    REFLECT_JSON(SaveShapesToJson, LoadShapesFromJson)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::ColliderComponent)
COMPONENT_REGISTER(CoreEngine::ColliderComponent)

namespace
{
    /// @brief 保存データの形状種別の名前を引く（知らない名前なら球にして警告する）
    CoreEngine::ColliderShapeType ParseShapeType(const std::string& name)
    {
        for (std::size_t i = 0; i < std::size(CoreEngine::kColliderShapeTypeNames); ++i) {
            if (name == CoreEngine::kColliderShapeTypeNames[i]) {
                return static_cast<CoreEngine::ColliderShapeType>(i);
            }
        }
        CoreEngine::Logger::GetInstance().Logf(CoreEngine::LogLevel::Warn, CoreEngine::LogCategory::System,
            "コライダーの形「{}」は知らないので、球として読みます", name);
        return CoreEngine::ColliderShapeType::Sphere;
    }

    /// @brief 保存データのレイヤーの名前を引く（知らない名前なら Default にして警告する）
    CoreEngine::CollisionLayer ParseLayer(const std::string& name)
    {
        CoreEngine::CollisionLayer layer = CoreEngine::CollisionLayer::Default;
        if (!CoreEngine::TryParseCollisionLayer(name, layer)) {
            CoreEngine::Logger::GetInstance().Logf(CoreEngine::LogLevel::Warn, CoreEngine::LogCategory::System,
                "コライダーのレイヤー「{}」は知らないので、Default として読みます", name);
        }
        return layer;
    }
}

namespace CoreEngine
{
    bool ColliderComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const ITransformSource*>(&other) != nullptr;
    }

    Collider& ColliderComponent::Add(const CollisionShape& shape, CollisionLayer layer)
    {
        // Collider は owner の GetWorldPosition()/GetWorldScale() を位置ソースにする。
        // 位置ソースが無いまま付けると全員が原点で重なる無音のバグになるので、ここで弾く。
        assert(GetOwner() && GetOwner()->GetComponent<ITransformSource>() &&
            "コライダーを付けるオブジェクトはトランスフォームコンポーネントを持つこと"
            "（TransformComponent / EulerTransformComponent のいずれか）");

        colliders_.push_back(std::make_unique<Collider>(GetOwner(), shape, layer));
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

    Collider& ColliderComponent::AddCapsule(float radius, float height, CollisionLayer layer,
                                            const Vector3& offset)
    {
        return Add(CollisionShape::MakeCapsule(radius, height, offset), layer);
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

    void ColliderComponent::SaveShapesToJson(json& parameters) const
    {
        json shapes = json::array();
        for (const auto& collider : colliders_) {
            if (!collider) { continue; }
            const CollisionShape& shape = collider->GetShape();
            json entry = json::object();
            entry["type"] = kColliderShapeTypeNames[static_cast<std::size_t>(shape.type)];
            entry["offset"] = JsonManager::Vector3ToJson(shape.offset);
            if (shape.type == ColliderShapeType::Sphere) {
                entry["radius"] = shape.radius;
            } else if (shape.type == ColliderShapeType::Capsule) {
                entry["radius"] = shape.radius;
                entry["height"] = shape.height;
            } else {
                entry["size"] = JsonManager::Vector3ToJson(shape.size);
            }
            entry["layer"] = ToString(collider->GetLayer());
            entry["enabled"] = collider->IsEnabled();
            entry["trigger"] = collider->IsTrigger();
            entry["static"] = collider->IsStatic();
            shapes.push_back(std::move(entry));
        }
        parameters["shapes"] = std::move(shapes);
    }

    void ColliderComponent::LoadShapesFromJson(const json& parameters)
    {
        const auto found = parameters.find("shapes");
        if (found == parameters.end() || !found->is_array()) {
            return;
        }
        const json& shapes = *found;

        // 余った分は取り外す（衝突判定が握っている間に消さないよう、実体の解放はフレーム末）
        while (colliders_.size() > shapes.size()) {
            if (colliders_.back()) {
                retired_.push_back(std::move(colliders_.back()));
            }
            colliders_.pop_back();
        }

        const json empty = json::object();
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            const json& entry = shapes[i].is_object() ? shapes[i] : empty;

            CollisionShape shape;
            shape.type = ParseShapeType(JsonManager::SafeGet<std::string>(entry, "type", kColliderShapeTypeNames[0]));
            shape.offset = JsonManager::SafeGetVector3(entry, "offset", shape.offset);
            shape.radius = JsonManager::SafeGet<float>(entry, "radius", shape.radius);
            shape.size = JsonManager::SafeGetVector3(entry, "size", shape.size);
            shape.height = JsonManager::SafeGet<float>(entry, "height", shape.height);
            const CollisionLayer layer = ParseLayer(JsonManager::SafeGet<std::string>(entry, "layer", ToString(CollisionLayer::Default)));

            if (i < colliders_.size() && colliders_[i]) {
                colliders_[i]->SetShape(shape);
                colliders_[i]->SetLayer(layer);
            } else if (i < colliders_.size()) {
                colliders_[i] = std::make_unique<Collider>(GetOwner(), shape, layer);
            } else {
                colliders_.push_back(std::make_unique<Collider>(GetOwner(), shape, layer));
            }

            Collider& collider = *colliders_[i];
            collider.SetEnabled(JsonManager::SafeGet<bool>(entry, "enabled", true));
            collider.SetTrigger(JsonManager::SafeGet<bool>(entry, "trigger", true));
            collider.SetStatic(JsonManager::SafeGet<bool>(entry, "static", false));
        }
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

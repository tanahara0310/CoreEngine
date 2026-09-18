#include "pch.h"
#include "ProbeComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"

namespace CollisionTest
{
    using namespace CoreEngine;

    void ProbeComponent::Start()
    {
        material_ = Sibling<MaterialComponent>();
        ApplyColor();
    }

    void ProbeComponent::HandleEnter(const CollisionInfo& info, bool trigger)
    {
        stats_.lastNormal = info.normal;
        stats_.lastDepth = info.depth;
        stats_.lastInfoValid = true;
        ++(trigger ? stats_.triggerEnter : stats_.collisionEnter);
        ProbeEvents::OnEnter(label_, stats_, info.other);
        ApplyColor();

        // A-2 の再現: 通知の中でコライダーを取り外す。
        // 実体の解放はフレーム末まで遅延するので判定ループの生ポインタは浮かない。
        if (removeColliderOnEnter_ && ProbeEvents::IsRemoveColliderInCallbackEnabled()) {
            if (GameObject* self = GetOwner()) {
                if (auto* selfColliders = self->GetComponent<ColliderComponent>()) {
                    selfColliders->RemoveAll();
                }
            }
        }
    }

    void ProbeComponent::HandleStay(const CollisionInfo& info)
    {
        stats_.lastNormal = info.normal;
        stats_.lastDepth = info.depth;
        stats_.lastInfoValid = true;
        ProbeEvents::OnStay(label_, stats_, info.other);
    }

    void ProbeComponent::HandleExit(const CollisionInfo& info)
    {
        ProbeEvents::OnExit(label_, stats_, info.other);
        ApplyColor();
    }

    void ProbeComponent::SetLabel(const std::string& label)
    {
        label_ = label;
        if (GameObject* owner = GetOwner()) {
            owner->SetName(label);
        }
    }

    WorldTransform& ProbeComponent::Transform() const
    {
        return GetOwner()->GetComponent<TransformComponent>()->Get();
    }

    Collider* ProbeComponent::FirstCollider() const
    {
        auto* const colliders = GetOwner()->GetComponent<ColliderComponent>();
        return colliders ? colliders->GetFirst() : nullptr;
    }

    ColliderComponent& ProbeComponent::Colliders() const
    {
        return *GetOwner()->GetOrAddComponent<ColliderComponent>();
    }

    void ProbeComponent::ApplyColor()
    {
        if (!material_) {
            material_ = Sibling<MaterialComponent>();
        }
        if (!material_) { return; }   // 見た目を持たないプローブ

        material_->SetColor(stats_.overlapDepth > 0 ? hitColor_ : baseColor_);
    }
}

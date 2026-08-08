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

        GameObject* owner = GetOwner();
        if (!owner) { return; }

        // コライダーのイベントを購読する（GameObject の override は不要）
        auto& colliders = owner->GetColliders();

        colliders.SetOnEnter([this](const CollisionInfo& info) {
            stats_.lastNormal = info.normal;
            stats_.lastDepth = info.depth;
            stats_.lastInfoValid = true;
            ProbeEvents::OnEnter(label_, stats_, info.other);
            ApplyColor();

            // A-2 の再現: コールバック中にコライダーを取り外す。
            // 実体の解放はフレーム末まで遅延するので判定ループの生ポインタは浮かない。
            if (removeColliderOnEnter_ && ProbeEvents::IsRemoveColliderInCallbackEnabled()) {
                if (GameObject* self = GetOwner()) {
                    self->RemoveCollider();
                }
            }
            });

        colliders.SetOnStay([this](const CollisionInfo& info) {
            stats_.lastNormal = info.normal;
            stats_.lastDepth = info.depth;
            stats_.lastInfoValid = true;
            ProbeEvents::OnStay(label_, stats_, info.other);
            });

        colliders.SetOnExit([this](const CollisionInfo& info) {
            ProbeEvents::OnExit(label_, stats_, info.other);
            ApplyColor();
            });

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
        return GetOwner()->GetCollider();
    }

    ColliderComponent& ProbeComponent::Colliders() const
    {
        return GetOwner()->GetColliders();
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

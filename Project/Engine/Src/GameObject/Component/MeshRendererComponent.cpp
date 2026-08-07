#include "pch.h"
#include "MeshRendererComponent.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Render/Culling/ModelVisibility.h"

namespace CoreEngine
{
    TransformComponent* MeshRendererComponent::ResolveTransform() const
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }

    BoundingBox MeshRendererComponent::GetWorldBoundingBox() const
    {
        const TransformComponent* transform = ResolveTransform();
        if (!model_ || !model_->GetModelResource() || !transform) {
            return BoundingBox();  // 無効な AABB
        }

        const BoundingBox& localAABB = model_->GetModelResource()->GetLocalBoundingBox();
        return localAABB.TransformBy(transform->Get().GetWorldMatrix());
    }

    bool MeshRendererComponent::DrawIfVisible(const DrawViewInfo& view)
    {
        if (!model_ || !view.view || !view.view->isValid) {
            return false;
        }

        TransformComponent* transform = ResolveTransform();
        if (!transform) {
            return false;
        }

        // 視錐台カリング（判定内容とデバッグトグルは ModelVisibility が持つ）
        if (!ModelVisibility::IsModelInView(view.view->frustum, GetWorldBoundingBox())) {
            return false;
        }

        model_->Draw(transform->Get(), view, texture_.gpuHandle);
        return true;
    }
}

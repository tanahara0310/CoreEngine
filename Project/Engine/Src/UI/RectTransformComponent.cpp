#include "pch.h"
#include "UI/RectTransformComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"

REFLECT_REGISTER(CoreEngine::RectTransformComponent)
COMPONENT_REGISTER(CoreEngine::RectTransformComponent)

namespace CoreEngine
{
    void RectTransformComponent::Awake()
    {
        ApplyRenderOrder();
    }

    void RectTransformComponent::ChangeAnchorKeepingPosition(UIAnchor anchor, const Vector2& canvasSize)
    {
        // 画面上の位置 = アンカーの点 + 位置 なので、アンカーの点の差だけ位置をずらす
        const Vector2 oldPoint = GetAnchorPoint(layout_.anchor, canvasSize);
        const Vector2 newPoint = GetAnchorPoint(anchor, canvasSize);
        layout_.anchoredPos.x += oldPoint.x - newPoint.x;
        layout_.anchoredPos.y += oldPoint.y - newPoint.y;
        layout_.anchor = anchor;
    }

    void RectTransformComponent::SetPivot(const Vector2& pivot)
    {
        if (layout_.pivot.x == pivot.x && layout_.pivot.y == pivot.y) {
            return;
        }
        layout_.pivot = pivot;
        ++shapeRevision_;
    }

    void RectTransformComponent::SetSize(const Vector2& size)
    {
        if (layout_.size.x == size.x && layout_.size.y == size.y) {
            return;
        }
        layout_.size = size;
        ++shapeRevision_;
    }

    void RectTransformComponent::SetSortOrder(int order)
    {
        layout_.sortOrder = order;
        ApplyRenderOrder();
    }

    void RectTransformComponent::ApplyRenderOrder()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->SetRenderOrder(kRenderOrderBase + layout_.sortOrder);
        }
    }
}

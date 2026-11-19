#include "pch.h"
#include "UI/UIScrollViewComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UILayoutGroupComponent.h"
#include "UI/UISliderComponent.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>

REFLECT_DEFINE_BEGIN(CoreEngine::UIScrollViewComponent, "UI スクロール")
    REFLECT_OBJECT_REF(content_, "中身")
    REFLECT_PROPERTY(horizontal_, "横に動く")
    REFLECT_PROPERTY(vertical_, "縦に動く")
    REFLECT_PROPERTY(inertia_, "慣性",
        p.tooltip = "入れると、ドラッグを離した後も速さが残って動き続ける")
    REFLECT_PROPERTY(decelerationRate_, "減速", p.range = Range(0.0f, 1.0f, 0.005f),
        p.tooltip = "1 秒後に残る速さの割合（0 ですぐ止まり、1 で止まらない）")
    REFLECT_PROPERTY(wheelStep_, "ホイール 1 目盛りの量", p.range = Range(0.0f, 4096.0f, 1.0f),
        p.tooltip = "0 なら、中身の UI を並べる部品の最初の子の大きさと間隔の和")
    REFLECT_OBJECT_REF(horizontalScrollbar_, "横のスクロールバー")
    REFLECT_OBJECT_REF(verticalScrollbar_, "縦のスクロールバー")
    REFLECT_ACCESSOR("normalizedPosition", "位置（0〜1）", GetNormalizedPosition, SetNormalizedPosition,
        p.flags = ::CoreEngine::Reflection::PropertyFlags::Hidden | ::CoreEngine::Reflection::PropertyFlags::NoSave)
    REFLECT_ACCESSOR("velocity", "速さ", GetVelocity, SetVelocity,
        p.flags = ::CoreEngine::Reflection::PropertyFlags::Hidden | ::CoreEngine::Reflection::PropertyFlags::NoSave)
    REFLECT_METHOD("StopMovement", "慣性を止める", StopMovement)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::UIScrollViewComponent)
COMPONENT_REGISTER(CoreEngine::UIScrollViewComponent)

namespace CoreEngine
{
    namespace
    {
        float AxisOf(const Vector2& value, int axis)
        {
            return axis == 0 ? value.x : value.y;
        }

        void SetAxisOf(Vector2& value, int axis, float component)
        {
            (axis == 0 ? value.x : value.y) = component;
        }

        /// 速さがこれより遅くなったら止める（px／秒）
        constexpr float kStopSpeed = 1.0f;
    }

    bool UIScrollViewComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    void UIScrollViewComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            rect_ = owner->GetOrAddComponent<RectTransformComponent>();
        }
    }

    RectTransformComponent* UIScrollViewComponent::GetRectTransform() const
    {
        return CachedSibling(rect_);
    }

    RectTransformComponent* UIScrollViewComponent::GetContent() const
    {
        return content_.Get();
    }

    void UIScrollViewComponent::SetContent(RectTransformComponent* content)
    {
        content_.Set(content);
    }

    void UIScrollViewComponent::SetVerticalScrollbar(UISliderComponent* scrollbar)
    {
        verticalScrollbar_.Set(scrollbar);
        scrollbarWritten_[1] = -1.0f;
    }

    void UIScrollViewComponent::SetHorizontalScrollbar(UISliderComponent* scrollbar)
    {
        horizontalScrollbar_.Set(scrollbar);
        scrollbarWritten_[0] = -1.0f;
    }

    bool UIScrollViewComponent::MeasureView(UIRect& outView, UIRect& outContent) const
    {
        const RectTransformComponent* const rect = GetRectTransform();
        const RectTransformComponent* const content = GetContent();
        if (!rect || !content || content->GetParent() != rect) {
            return false;
        }
        outView = rect->GetLocalRect(rect->FindCanvasSize());
        outContent = content->GetLayout().CalculateRect(outView);
        return true;
    }

    Vector2 UIScrollViewComponent::OffsetOf(const UIRect& view, const UIRect& content)
    {
        return { content.min.x - view.min.x, content.min.y - view.min.y };
    }

    void UIScrollViewComponent::MoveContent(const Vector2& delta, bool respectAxes)
    {
        RectTransformComponent* const content = GetContent();
        UIRect view;
        UIRect contentRect;
        if (!content || !MeasureView(view, contentRect)) {
            return;
        }

        const Vector2 offset = OffsetOf(view, contentRect);
        const Vector2 viewSize = view.Size();
        const Vector2 contentSize = contentRect.Size();
        Vector2 moved = { 0.0f, 0.0f };
        for (int axis = 0; axis < 2; ++axis) {
            const bool movable = axis == 0 ? horizontal_ : vertical_;
            if (respectAxes && !movable) {
                continue;
            }
            // 中身の先頭は窓の先頭より後ろへ行かず、末尾は窓の末尾より前へ来ない
            const float low = (std::min)(AxisOf(viewSize, axis) - AxisOf(contentSize, axis), 0.0f);
            const float wanted = AxisOf(offset, axis) + AxisOf(delta, axis);
            const float clamped = std::clamp(wanted, low, 0.0f);
            if (clamped != wanted) {
                SetAxisOf(velocity_, axis, 0.0f);
            }
            SetAxisOf(moved, axis, clamped - AxisOf(offset, axis));
        }
        if (moved.x != 0.0f || moved.y != 0.0f) {
            const Vector2 position = content->GetAnchoredPosition();
            content->SetAnchoredPosition({ position.x + moved.x, position.y + moved.y });
        }
    }

    Vector2 UIScrollViewComponent::GetNormalizedPosition() const
    {
        UIRect view;
        UIRect content;
        if (!MeasureView(view, content)) {
            return { 0.0f, 0.0f };
        }
        const Vector2 offset = OffsetOf(view, content);
        Vector2 normalized = { 0.0f, 0.0f };
        for (int axis = 0; axis < 2; ++axis) {
            const float scrollable = AxisOf(content.Size(), axis) - AxisOf(view.Size(), axis);
            if (scrollable > 0.0f) {
                SetAxisOf(normalized, axis, std::clamp(-AxisOf(offset, axis) / scrollable, 0.0f, 1.0f));
            }
        }
        return normalized;
    }

    void UIScrollViewComponent::SetNormalizedPosition(const Vector2& normalized)
    {
        UIRect view;
        UIRect content;
        if (!MeasureView(view, content)) {
            return;
        }
        const Vector2 offset = OffsetOf(view, content);
        Vector2 delta = { 0.0f, 0.0f };
        for (int axis = 0; axis < 2; ++axis) {
            const float scrollable = (std::max)(AxisOf(content.Size(), axis) - AxisOf(view.Size(), axis), 0.0f);
            const float target = -std::clamp(AxisOf(normalized, axis), 0.0f, 1.0f) * scrollable;
            SetAxisOf(delta, axis, target - AxisOf(offset, axis));
        }
        MoveContent(delta, false);
    }

    void UIScrollViewComponent::StopMovement()
    {
        velocity_ = { 0.0f, 0.0f };
    }

    void UIScrollViewComponent::ScrollTo(const RectTransformComponent& target)
    {
        const RectTransformComponent* const rect = GetRectTransform();
        const RectTransformComponent* const content = GetContent();
        UIRect view;
        UIRect contentRect;
        if (!rect || !content || !content->IsSelfOrDescendant(&target) || !MeasureView(view, contentRect)) {
            return;
        }

        // 相手の 4 隅を窓の座標へ戻して囲む
        const Vector2 canvasSize = rect->FindCanvasSize();
        const UIQuad quad = target.GetCanvasQuad(canvasSize);
        const Vector2 corners[4] = { quad.topLeft, quad.topRight, quad.bottomRight, quad.bottomLeft };
        UIRect bounds;
        for (int i = 0; i < 4; ++i) {
            Vector2 local;
            if (!rect->CanvasToLocal(corners[i], canvasSize, local)) {
                return;
            }
            if (i == 0) {
                bounds.min = local;
                bounds.max = local;
            } else {
                bounds.min = { (std::min)(bounds.min.x, local.x), (std::min)(bounds.min.y, local.y) };
                bounds.max = { (std::max)(bounds.max.x, local.x), (std::max)(bounds.max.y, local.y) };
            }
        }

        Vector2 delta = { 0.0f, 0.0f };
        for (int axis = 0; axis < 2; ++axis) {
            const float viewMin = AxisOf(view.min, axis);
            const float viewMax = AxisOf(view.max, axis);
            const float targetMin = AxisOf(bounds.min, axis);
            const float targetMax = AxisOf(bounds.max, axis);
            if (targetMin < viewMin || targetMax - targetMin > viewMax - viewMin) {
                SetAxisOf(delta, axis, viewMin - targetMin);
            } else if (targetMax > viewMax) {
                SetAxisOf(delta, axis, viewMax - targetMax);
            }
        }
        StopMovement();
        MoveContent(delta, true);
    }

    float UIScrollViewComponent::WheelStep(int axis) const
    {
        if (wheelStep_ > 0.0f) {
            return wheelStep_;
        }
        const RectTransformComponent* const content = GetContent();
        const GameObject* const owner = content ? content->GetOwner() : nullptr;
        const UILayoutGroupComponent* const group = owner ? owner->GetComponent<UILayoutGroupComponent>() : nullptr;
        const float step = group ? group->GetFirstChildStep(axis) : 0.0f;
        return step > 0.0f ? step : kFallbackWheelStep;
    }

    void UIScrollViewComponent::Scroll(float notches)
    {
        if (notches == 0.0f) {
            return;
        }
        // 縦に動くなら縦へ、横だけなら横へ動かす
        const int axis = vertical_ ? 1 : (horizontal_ ? 0 : -1);
        if (axis < 0) {
            return;
        }
        Vector2 delta = { 0.0f, 0.0f };
        SetAxisOf(delta, axis, notches * WheelStep(axis));
        StopMovement();
        MoveContent(delta, true);
    }

    void UIScrollViewComponent::BeginDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
    {
        const RectTransformComponent* const rect = GetRectTransform();
        UIRect view;
        UIRect content;
        Vector2 local;
        if (!rect || !MeasureView(view, content) || !rect->CanvasToLocal(pointerOnCanvas, canvasSize, local)) {
            return;
        }
        dragging_ = true;
        dragStartPointer_ = local;
        dragStartOffset_ = OffsetOf(view, content);
        lastDragOffset_ = dragStartOffset_;
        lastDragFrame_ = Time::FrameCount();
        StopMovement();
    }

    void UIScrollViewComponent::Drag(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
    {
        const RectTransformComponent* const rect = GetRectTransform();
        UIRect view;
        UIRect content;
        Vector2 local;
        if (!dragging_ || !rect || !MeasureView(view, content)
            || !rect->CanvasToLocal(pointerOnCanvas, canvasSize, local)) {
            return;
        }

        // 押した所からの動きだけ中身をずらす（端を越えた分は止まる）
        const Vector2 offset = OffsetOf(view, content);
        const Vector2 target = {
            dragStartOffset_.x + (local.x - dragStartPointer_.x),
            dragStartOffset_.y + (local.y - dragStartPointer_.y),
        };
        MoveContent({ target.x - offset.x, target.y - offset.y }, true);

        // 動いた量から速さを測り、急に変わらないよう前の速さとならす
        if (MeasureView(view, content)) {
            const Vector2 now = OffsetOf(view, content);
            const float deltaTime = Time::UnscaledDeltaTime();
            if (deltaTime > 0.0f) {
                const float blend = (std::min)(deltaTime * 10.0f, 1.0f);
                velocity_.x += ((now.x - lastDragOffset_.x) / deltaTime - velocity_.x) * blend;
                velocity_.y += ((now.y - lastDragOffset_.y) / deltaTime - velocity_.y) * blend;
            }
            lastDragOffset_ = now;
        }
        lastDragFrame_ = Time::FrameCount();
    }

    void UIScrollViewComponent::EndDrag()
    {
        dragging_ = false;
        if (!inertia_) {
            StopMovement();
        }
    }

    void UIScrollViewComponent::LateUpdate()
    {
        // ドラッグの知らせが途切れた（相手が消えた・止められた）ら、離したことにする
        if (dragging_ && lastDragFrame_ + 1 < Time::FrameCount()) {
            EndDrag();
        }

        Vector2 delta = { 0.0f, 0.0f };
        if (!dragging_) {
            if (inertia_) {
                const float deltaTime = Time::UnscaledDeltaTime();
                delta = { velocity_.x * deltaTime, velocity_.y * deltaTime };
                const float keep = std::pow(std::clamp(decelerationRate_, 0.0f, 1.0f), deltaTime);
                velocity_ = { velocity_.x * keep, velocity_.y * keep };
                if (std::fabs(velocity_.x) < kStopSpeed) { velocity_.x = 0.0f; }
                if (std::fabs(velocity_.y) < kStopSpeed) { velocity_.y = 0.0f; }
            } else {
                StopMovement();
            }
        }
        // 中身が縮んで端を越えていても、ここで戻す
        MoveContent(delta, true);
        SyncScrollbars();
    }

    void UIScrollViewComponent::SyncScrollbars()
    {
        UISliderComponent* const bars[2] = { horizontalScrollbar_.Get(), verticalScrollbar_.Get() };
        for (int axis = 0; axis < 2; ++axis) {
            UISliderComponent* const bar = bars[axis];
            if (!bar) {
                scrollbarWritten_[axis] = -1.0f;
                continue;
            }
            const float current = bar->GetNormalizedValue();
            if (scrollbarWritten_[axis] >= 0.0f && std::fabs(current - scrollbarWritten_[axis]) > 1e-4f) {
                // スクロールバーを動かされたので、中身をそちらへ合わせる
                Vector2 normalized = GetNormalizedPosition();
                SetAxisOf(normalized, axis, current);
                SetAxisOf(velocity_, axis, 0.0f);
                SetNormalizedPosition(normalized);
            }
            bar->SetNormalizedValue(AxisOf(GetNormalizedPosition(), axis));
            scrollbarWritten_[axis] = bar->GetNormalizedValue();
        }
    }
}

#include "pch.h"
#include "UI/UISliderComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIImageComponent.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>

REFLECT_REGISTER(CoreEngine::UISliderComponent)
COMPONENT_REGISTER(CoreEngine::UISliderComponent)

namespace CoreEngine
{
    void UISliderComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
        ApplyVisual();
        // 繋いだ相手がもう居れば、再生を始める前から値どおりの見た目にする
        ApplyLayout();
    }

    void UISliderComponent::LateUpdate()
    {
        ApplyLayout();
    }

    void UISliderComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        (void)property;
        ApplyVisual();
        ApplyLayout();
    }

    bool UISliderComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const RectTransformComponent*>(&other) != nullptr;
    }

    bool UISliderComponent::WasChanged() const
    {
        return changedFrame_ != 0 && Time::FrameCount() == changedFrame_ + 1;
    }

    float UISliderComponent::Span() const
    {
        return std::max(maxValue_ - minValue_, 0.0001f);
    }

    void UISliderComponent::SetValue(float value)
    {
        float next = std::clamp(value, minValue_, maxValue_);
        if (wholeNumbers_) {
            next = std::round(next);
        }
        if (next == value_) {
            return;
        }
        value_ = next;
        changedFrame_ = Time::FrameCount();
        ApplyLayout();
    }

    float UISliderComponent::GetNormalizedValue() const
    {
        return (value_ - minValue_) / Span();
    }

    void UISliderComponent::SetNormalizedValue(float normalized)
    {
        SetValue(minValue_ + std::clamp(normalized, 0.0f, 1.0f) * Span());
    }

    void UISliderComponent::SetMinValue(float value)
    {
        minValue_ = value;
        SetValue(value_);
    }

    void UISliderComponent::SetMaxValue(float value)
    {
        maxValue_ = value;
        SetValue(value_);
    }

    void UISliderComponent::SetWholeNumbers(bool wholeNumbers)
    {
        wholeNumbers_ = wholeNumbers;
        if (wholeNumbers_) {
            SetValue(value_);
        }
    }

    void UISliderComponent::SetDirection(Direction direction)
    {
        direction_ = direction;
        // 伸びる側が入れ替わるので、置き直しの判定を無効にする
        laidOutNormalized_ = -1.0f;
    }

    void UISliderComponent::SetInteractable(bool value)
    {
        if (interactable_ == value) {
            return;
        }
        interactable_ = value;
        if (!interactable_) {
            // 動かせなくした瞬間は、乗っている・選んでいる・掴んでいる状態を持ち越さない
            hovered_ = false;
            focused_ = false;
            pressed_ = false;
        }
        ApplyVisual();
    }

    RectTransformComponent* UISliderComponent::GetRectTransform() const
    {
        return Sibling<RectTransformComponent>();
    }

    int UISliderComponent::GetPointerSortOrder() const
    {
        const RectTransformComponent* const rect = GetRectTransform();
        return rect ? rect->GetSortOrder() : 0;
    }

    void UISliderComponent::OnPointerEnter()
    {
        hovered_ = true;
        ApplyVisual();
    }

    void UISliderComponent::OnPointerExit()
    {
        hovered_ = false;
        ApplyVisual();
    }

    void UISliderComponent::OnFocusEnter()
    {
        focused_ = true;
        ApplyVisual();
    }

    void UISliderComponent::OnFocusExit()
    {
        focused_ = false;
        ApplyVisual();
    }

    void UISliderComponent::OnPressBegin()
    {
        pressed_ = true;
        ApplyVisual();
    }

    void UISliderComponent::OnPressEnd(bool onSelf)
    {
        (void)onSelf;
        pressed_ = false;
        ApplyVisual();
    }

    void UISliderComponent::OnDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
    {
        if (interactable_) {
            ApplyPointer(pointerOnCanvas, canvasSize);
        }
    }

    bool UISliderComponent::OnNavigate(UINavigationDirection direction)
    {
        if (!interactable_) {
            return false;
        }
        // 自分と同じ軸の入力だけ使う。直交する向きはフォーカス送りへ渡す
        const bool horizontalInput = direction == UINavigationDirection::Left
                                  || direction == UINavigationDirection::Right;
        if (horizontalInput != IsHorizontal()) {
            return false;
        }

        // 整数だけのときは 1 未満の刻みでは動かないので、最低 1 にする
        float step = navigationStep_ > 0.0f ? navigationStep_ : Span() * 0.1f;
        if (wholeNumbers_) {
            step = std::max(std::round(step), 1.0f);
        }

        // 画面の右・下へ進む入力を「座標が増える向き」とし、値が増える向きが逆なら符号を返す
        const bool towardLargerCoordinate = direction == UINavigationDirection::Right
                                         || direction == UINavigationDirection::Down;
        const float sign = (towardLargerCoordinate != IsReversed()) ? 1.0f : -1.0f;
        SetValue(value_ + sign * step);
        return true;
    }

    void UISliderComponent::ApplyPointer(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
    {
        const RectTransformComponent* const rect = GetRectTransform();
        if (!rect) {
            return;
        }
        const UIRect track = rect->GetLayout().CalculateRect(canvasSize);
        const Vector2 size = track.Size();

        const float length = IsHorizontal() ? size.x : size.y;
        if (length <= 0.0f) {
            return;
        }
        const float along = IsHorizontal() ? (pointerOnCanvas.x - track.min.x)
                                           : (pointerOnCanvas.y - track.min.y);

        float normalized = std::clamp(along / length, 0.0f, 1.0f);
        if (IsReversed()) {
            normalized = 1.0f - normalized;
        }
        SetNormalizedValue(normalized);
    }

    const Vector4& UISliderComponent::CurrentColor() const
    {
        if (!interactable_)       { return disabledColor_; }
        if (pressed_)             { return pressedColor_; }
        if (hovered_ || focused_) { return hoveredColor_; }
        return normalColor_;
    }

    void UISliderComponent::ApplyVisual()
    {
        // つまみを繋いでいればそちらを、繋いでいなければ帯そのものを塗る
        UIImageComponent* target = nullptr;
        if (RectTransformComponent* const handle = handle_.Get()) {
            target = handle->Sibling<UIImageComponent>();
        }
        if (!target) {
            target = Sibling<UIImageComponent>();
        }
        if (target) {
            target->SetColor(CurrentColor());
        }
    }

    bool UISliderComponent::LayoutIsUpToDate(const Vector2& trackPosition, const Vector2& trackSize,
                                             float normalized) const
    {
        return laidOutNormalized_ == normalized
            && laidOutPosition_.x == trackPosition.x && laidOutPosition_.y == trackPosition.y
            && laidOutSize_.x == trackSize.x && laidOutSize_.y == trackSize.y;
    }

    void UISliderComponent::ApplyLayout()
    {
        RectTransformComponent* const fill = fill_.Get();
        RectTransformComponent* const handle = handle_.Get();
        if (!fill && !handle) {
            return;
        }
        const RectTransformComponent* const rect = GetRectTransform();
        if (!rect) {
            return;
        }

        const UILayout& track = rect->GetLayout();
        const float normalized = std::clamp(GetNormalizedValue(), 0.0f, 1.0f);
        if (LayoutIsUpToDate(track.anchoredPos, track.size, normalized)) {
            return;
        }
        laidOutPosition_ = track.anchoredPos;
        laidOutSize_ = track.size;
        laidOutNormalized_ = normalized;

        // アンカーは画面基準なので、繋いだ相手にも帯と同じアンカーを使わせて座標系を揃える。
        // 揃えたうえで、帯の左上からの距離で置く
        const Vector2 trackMin = {
            track.anchoredPos.x - track.pivot.x * track.size.x,
            track.anchoredPos.y - track.pivot.y * track.size.y,
        };
        const Vector2 trackCenter = {
            trackMin.x + track.size.x * 0.5f,
            trackMin.y + track.size.y * 0.5f,
        };

        const float length = IsHorizontal() ? track.size.x : track.size.y;
        const float filledLength = length * normalized;
        // 値が増える向きが座標と逆なら、埋まるのは反対側の端から
        const float filledStart = IsReversed() ? (length - filledLength) : 0.0f;

        // 中心に置きたい点から、基準点の置き場所を逆算する
        const auto place = [](RectTransformComponent& target, const Vector2& center) {
            const Vector2 pivot = target.GetPivot();
            const Vector2 size = target.GetSize();
            target.SetAnchoredPosition({ center.x + (pivot.x - 0.5f) * size.x,
                                         center.y + (pivot.y - 0.5f) * size.y });
            };

        if (fill) {
            fill->SetAnchor(track.anchor);
            Vector2 size = fill->GetSize();
            Vector2 center = trackCenter;
            if (IsHorizontal()) {
                size.x = filledLength;
                center.x = trackMin.x + filledStart + filledLength * 0.5f;
            }
            else {
                size.y = filledLength;
                center.y = trackMin.y + filledStart + filledLength * 0.5f;
            }
            fill->SetSize(size);
            place(*fill, center);
        }

        if (handle) {
            handle->SetAnchor(track.anchor);
            // つまみは埋まっている側の先端に置く
            const float along = IsReversed() ? (length - filledLength) : filledLength;
            Vector2 center = trackCenter;
            if (IsHorizontal()) {
                center.x = trackMin.x + along;
            }
            else {
                center.y = trackMin.y + along;
            }
            place(*handle, center);
        }
    }
}

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
    UISliderComponent::UISliderComponent()
        : UISelectableComponent({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.85f, 0.9f, 1.0f, 1.0f },
                                { 0.7f, 0.78f, 0.95f, 1.0f }, { 0.6f, 0.6f, 0.6f, 0.5f })
    {
    }

    void UISliderComponent::Awake()
    {
        UISelectableComponent::Awake();
        // 繋いだ相手がもう居れば、再生を始める前から値どおりの見た目にする
        ApplyLayout();
    }

    void UISliderComponent::LateUpdate()
    {
        ApplyLayout();
    }

    void UISliderComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        UISelectableComponent::OnPropertyChanged(property);
        ApplyLayout();
    }

    bool UISliderComponent::WasChanged() const
    {
        return IsFrameAfter(changedFrame_);
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

    void UISliderComponent::OnDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
    {
        if (IsInteractable()) {
            ApplyPointer(pointerOnCanvas, canvasSize);
        }
    }

    bool UISliderComponent::OnNavigate(UINavigationDirection direction)
    {
        if (!IsInteractable()) {
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

        // 親のある帯は、自分の座標（回転と拡大を外した座標）へ戻して測る
        UIRect track = rect->GetCanvasBounds(canvasSize);
        Vector2 pointer = pointerOnCanvas;
        if (rect->GetParent()) {
            if (!rect->CanvasToLocal(pointerOnCanvas, canvasSize, pointer)) {
                return;
            }
            track = rect->GetLocalRect(canvasSize);
        }
        const Vector2 size = track.Size();

        const float length = IsHorizontal() ? size.x : size.y;
        if (length <= 0.0f) {
            return;
        }
        const float along = IsHorizontal() ? (pointer.x - track.min.x)
                                           : (pointer.y - track.min.y);

        float normalized = std::clamp(along / length, 0.0f, 1.0f);
        if (IsReversed()) {
            normalized = 1.0f - normalized;
        }
        SetNormalizedValue(normalized);
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

        // 帯の子にした相手は、帯の左上にアンカーを付けて帯の中で置く。
        // 子でない相手は、帯と同じアンカーを使わせて座標系を揃える
        const auto anchorTo = [&](RectTransformComponent& target, const Vector2& topLeftOffset) {
            if (target.GetParent() == rect) {
                target.SetAnchor(UIAnchor::TopLeft);
                return topLeftOffset;
            }
            target.SetAnchors(track.anchorMin, track.anchorMax);
            return trackMin;
        };

        if (fill) {
            const Vector2 origin = anchorTo(*fill, { 0.0f, 0.0f });
            Vector2 size = fill->GetSize();
            Vector2 center = fill->GetParent() == rect
                ? Vector2{ track.size.x * 0.5f, track.size.y * 0.5f } : trackCenter;
            if (IsHorizontal()) {
                size.x = filledLength;
                center.x = origin.x + filledStart + filledLength * 0.5f;
            }
            else {
                size.y = filledLength;
                center.y = origin.y + filledStart + filledLength * 0.5f;
            }
            fill->SetSize(size);
            place(*fill, center);
        }

        if (handle) {
            const Vector2 origin = anchorTo(*handle, { 0.0f, 0.0f });
            // つまみは埋まっている側の先端に置く
            const float along = IsReversed() ? (length - filledLength) : filledLength;
            Vector2 center = handle->GetParent() == rect
                ? Vector2{ track.size.x * 0.5f, track.size.y * 0.5f } : trackCenter;
            if (IsHorizontal()) {
                center.x = origin.x + along;
            }
            else {
                center.y = origin.y + along;
            }
            place(*handle, center);
        }
    }
}

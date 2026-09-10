#include "pch.h"
#include "TitleButtonAnimationComponent.h"

#include "GameObject/GameObject.h"
#include "UI/UIImage.h"
#include "Utility/Tween/Tween.h"

#include <utility>

using namespace CoreEngine;

void GameComponents::TitleButtonAnimationComponent::Start()
{
    button_ = dynamic_cast<UIImage*>(GetOwner());
    if (!button_) {
        SetEnabled(false);
        return;
    }

    targetSize_ = button_->GetSize();
    normalColor_ = button_->GetColor();
    started_ = true;

    const Vector4 visibleColor = selected_ ? selectedColor_ : normalColor_;
    Vector4 hiddenColor = visibleColor;
    hiddenColor.w = 0.0f;

    button_->SetSize({ targetSize_.x, 0.0f });
    button_->SetColor(hiddenColor);

    TweenSequence()
        .Append(
            Tween::To<Vector2>(
                Vector2{ targetSize_.x, 0.0f },
                targetSize_,
                introDuration_,
                [this](const Vector2& size) {
                    if (button_) { button_->SetSize(size); }
                })
                .SetEase(EasingUtil::Type::EaseOutBack))
        .Join(
            Tween::To<Vector4>(
                hiddenColor,
                visibleColor,
                introDuration_,
                [this](const Vector4& color) {
                    if (button_) { button_->SetColor(color); }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .SetDelay(introDelay_)
        .SetLink(button_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_button_intro")
        .OnComplete([this] { StartIdleAnimation(); });
}

void GameComponents::TitleButtonAnimationComponent::StartIdleAnimation()
{
    if (!button_) {
        return;
    }

    Tween::KillById("title_button_idle");

    const Vector2 idleSize = {
        targetSize_.x * 1.025f,
        targetSize_.y * 1.025f,
    };

    Tween::To<Vector2>(
        targetSize_,
        idleSize,
        1.15f,
        [this](const Vector2& size) {
            if (button_) { button_->SetSize(size); }
        })
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo)
        .SetLink(button_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_button_idle");
}

void GameComponents::TitleButtonAnimationComponent::SetSelected(bool selected)
{
    selected_ = selected;
    if (!button_ || !started_) {
        return;
    }

    Tween::KillById("title_button_selected");

    const Vector4 from = button_->GetColor();
    const Vector4 to = selected_ ? selectedColor_ : normalColor_;

    Tween::To<Vector4>(
        from,
        to,
        0.12f,
        [this](const Vector4& color) {
            if (button_) { button_->SetColor(color); }
        })
        .SetEase(EasingUtil::Type::EaseOutCubic)
        .SetLink(button_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_button_selected");
}

void GameComponents::TitleButtonAnimationComponent::PlayPressAnimation(
    std::function<void()> onFinished)
{
    if (!button_) {
        if (onFinished) { onFinished(); }
        return;
    }

    Tween::KillById("title_button_intro");
    Tween::KillById("title_button_idle");
    Tween::KillById("title_button_selected");

    const Vector2 currentSize = button_->GetSize();
    const Vector2 pressedSize = {
        targetSize_.x * 0.92f,
        targetSize_.y * 0.92f,
    };

    TweenSequence sequence;
    sequence
        .Append(
            Tween::To<Vector2>(
                currentSize,
                pressedSize,
                0.08f,
                [this](const Vector2& size) {
                    if (button_) { button_->SetSize(size); }
                })
                .SetEase(EasingUtil::Type::EaseInOutCubic))
        .Append(
            Tween::To<Vector2>(
                pressedSize,
                targetSize_,
                0.12f,
                [this](const Vector2& size) {
                    if (button_) { button_->SetSize(size); }
                })
                .SetEase(EasingUtil::Type::EaseOutBack))
        .AppendCallback(std::move(onFinished))
        .SetLink(button_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_button_press");
}

void GameComponents::TitleButtonAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

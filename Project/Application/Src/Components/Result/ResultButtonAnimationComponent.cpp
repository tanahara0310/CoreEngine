#include "pch.h"
#include "ResultButtonAnimationComponent.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include "GameObject/GameObject.h"
#include "UI/UIText.h"
#include "Utility/Tween/Tween.h"

#include <utility>

using namespace CoreEngine;

namespace GameComponents
{
    CVar<float> ResultButtonAnimationComponent::ReactionScale{
        "Result.Animation.ReactionScale",
        0.88f,
        "リザルトボタンの選択・決定時に一瞬だけ縮小する倍率",
        CVarRange{ 0.5f, 1.0f } };

    CVar<float> ResultButtonAnimationComponent::ReactionDuration{
        "Result.Animation.ReactionDuration",
        0.22f,
        "リザルトボタンの縮小リアクション時間（秒）",
        CVarRange{ 0.05f, 1.0f } };

    CVar<float> ResultButtonAnimationComponent::SelectedScale{
        "Result.Animation.SelectedScale",
        1.08f,
        "選択中リザルトボタンの表示倍率",
        CVarRange{ 1.0f, 1.5f } };

    CVar<float> ResultButtonAnimationComponent::ConfirmScale{
        "Result.Animation.ConfirmScale",
        1.28f,
        "決定時にリザルトボタンが一瞬大きくなる倍率",
        CVarRange{ 1.0f, 3.0f } };

    CVar<float> ResultButtonAnimationComponent::ConfirmDuration{
        "Result.Animation.ConfirmDuration",
        0.36f,
        "決定時の拡大・縮小・フェード時間（秒）",
        CVarRange{ 0.05f, 1.5f } };

    CVar<float> ResultButtonAnimationComponent::UnselectedFadeScale{
        "Result.Animation.UnselectedFadeScale",
        0.82f,
        "未選択リザルトボタンがフェード時に縮小する倍率",
        CVarRange{ 0.5f, 1.0f } };

    CVar<float> ResultButtonAnimationComponent::UnselectedFadeDuration{
        "Result.Animation.UnselectedFadeDuration",
        0.18f,
        "未選択リザルトボタンの縮小・フェード時間（秒）",
        CVarRange{ 0.05f, 1.0f } };

    CVar<float> ResultButtonAnimationComponent::SelectedBobDistance{
        "Result.Animation.SelectedBobDistance",
        7.0f,
        "選択中ボタンが上下に動く距離（ピクセル）",
        CVarRange{ 0.0f, 40.0f } };

    CVar<float> ResultButtonAnimationComponent::SelectedBobDuration{
        "Result.Animation.SelectedBobDuration",
        0.75f,
        "選択中ボタンが上下に動く片道時間（秒）",
        CVarRange{ 0.1f, 3.0f } };

    CVar<Vector4> ResultButtonAnimationComponent::SelectedColor{
        "Result.UI.SelectedButtonColor",
        { 0.30f, 0.72f, 1.0f, 1.0f },
        "選択中リザルトボタンの文字色" };
}

#ifdef USE_IMGUI
bool GameComponents::ResultButtonAnimationComponent::DrawInspector()
{
    return CoreEngine::CVarUI::DrawTree("Result.Animation")
        | CoreEngine::CVarUI::DrawTree("Result.UI");
}
#endif

void GameComponents::ResultButtonAnimationComponent::Start()
{
    text_ = dynamic_cast<UIText*>(GetOwner());
    if (!text_) {
        SetEnabled(false);
        return;
    }

    normalColor_ = text_->GetColor();
    baseFontSize_ = text_->GetFontSize();
    basePositionY_ = text_->GetAnchoredPosition().y;
    started_ = true;

    text_->SetFontSize(selected_ ? baseFontSize_ * SelectedScale.Get() : baseFontSize_);
    text_->SetColor(selected_ ? SelectedColor.Get() : normalColor_);
    if (selected_) {
        StartSelectedIdle();
    }
}

void GameComponents::ResultButtonAnimationComponent::SetSelected(bool selected)
{
    selected_ = selected;
    if (!text_ || !started_) {
        return;
    }

    Tween::KillById(tweenId_ + "_color");

    const Vector4 from = text_->GetColor();
    const Vector4 to = selected_ ? SelectedColor.Get() : normalColor_;
    Tween::To<Vector4>(
        from,
        to,
        0.10f,
        [this](const Vector4& color) {
            ApplyColor(color);
        })
        .SetEase(EasingUtil::Type::EaseOutCubic)
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_color");

    if (selected_) {
        text_->SetFontSize(baseFontSize_ * SelectedScale.Get());
    } else {
        Tween::KillById(tweenId_ + "_reaction");
        Tween::KillById(tweenId_ + "_confirm");
        Tween::KillById(tweenId_ + "_bob");
        text_->SetAnchoredPosition({ text_->GetAnchoredPosition().x, basePositionY_ });
        text_->SetFontSize(baseFontSize_);
    }
}

void GameComponents::ResultButtonAnimationComponent::PlaySelectionReaction()
{
    if (!text_ || !selected_) {
        return;
    }

    PlayScaleReaction([this] {
        if (selected_) {
            StartSelectedIdle();
        }
    });
}

void GameComponents::ResultButtonAnimationComponent::PlayConfirmReaction(
    std::function<void()> onFinished)
{
    if (!text_) {
        if (onFinished) {
            onFinished();
        }
        return;
    }

    StopTweens();

    const float duration = ConfirmDuration.Get();
    const float growDuration = duration * 0.30f;
    const float fadeDuration = duration - growDuration;
    const float currentFontSize = text_->GetFontSize();
    const float selectedFontSize = baseFontSize_ * SelectedScale.Get();
    const float peakFontSize = selectedFontSize * ConfirmScale.Get();
    const Vector4 startColor = text_->GetColor();
    Vector4 endColor = startColor;
    endColor.w = 0.0f;

    TweenSequence sequence;
    sequence
        .Append(
            Tween::To<float>(
                currentFontSize,
                peakFontSize,
                growDuration,
                [this](const float& fontSize) {
                    if (text_) {
                        text_->SetFontSize(fontSize);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .Append(
            Tween::To<float>(
                peakFontSize,
                selectedFontSize,
                fadeDuration,
                [this](const float& fontSize) {
                    if (text_) {
                        text_->SetFontSize(fontSize);
                    }
                })
                .SetEase(EasingUtil::Type::EaseInCubic)
        )
        .Join(
            Tween::To<Vector4>(
                startColor,
                endColor,
                fadeDuration,
                [this](const Vector4& color) {
                    ApplyColor(color);
                })
                .SetEase(EasingUtil::Type::EaseInCubic))
        .AppendCallback(std::move(onFinished))
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_confirm");
}

void GameComponents::ResultButtonAnimationComponent::PlayUnselectedFade()
{
    if (!text_) {
        return;
    }

    StopTweens();

    const float duration = UnselectedFadeDuration.Get();
    const float currentFontSize = text_->GetFontSize();
    const float targetFontSize = baseFontSize_ * UnselectedFadeScale.Get();
    const Vector4 startColor = text_->GetColor();
    Vector4 endColor = startColor;
    endColor.w = 0.0f;

    Tween::To<float>(
        currentFontSize,
        targetFontSize,
        duration,
        [this](const float& fontSize) {
            if (text_) {
                text_->SetFontSize(fontSize);
            }
        })
        .SetEase(EasingUtil::Type::EaseInCubic)
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_unselected_fade");

    Tween::To<Vector4>(
        startColor,
        endColor,
        duration,
        [this](const Vector4& color) {
            ApplyColor(color);
        })
        .SetEase(EasingUtil::Type::EaseInCubic)
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_unselected_fade_color");
}

void GameComponents::ResultButtonAnimationComponent::PlayScaleReaction(
    std::function<void()> onFinished)
{
    StopTweens();

    const float duration = ReactionDuration.Get();
    const float growDuration = duration * 0.35f;
    const float restoreDuration = duration - growDuration;
    const float selectedFontSize = baseFontSize_ * SelectedScale.Get();
    const float pressedFontSize = selectedFontSize * ReactionScale.Get();
    const float currentFontSize = text_->GetFontSize();

    TweenSequence sequence;
    sequence
        .Append(
            Tween::To<float>(
                currentFontSize,
                pressedFontSize,
                growDuration,
                [this](const float& fontSize) {
                    if (text_) {
                        text_->SetFontSize(fontSize);
                    }
                })
                .SetEase(EasingUtil::Type::EaseInCubic))
        .Append(
            Tween::To<float>(
                pressedFontSize,
                selectedFontSize,
                restoreDuration,
                [this](const float& fontSize) {
                    if (text_) {
                        text_->SetFontSize(fontSize);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutBack))
        .AppendCallback(std::move(onFinished))
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_reaction");
}

void GameComponents::ResultButtonAnimationComponent::StartSelectedIdle()
{
    if (!text_ || !selected_) {
        return;
    }

    Tween::KillById(tweenId_ + "_bob");

    const float fromY = basePositionY_;
    const float toY = basePositionY_ - SelectedBobDistance.Get();
    Tween::To<float>(
        fromY,
        toY,
        SelectedBobDuration.Get(),
        [this](const float& positionY) {
            if (text_) {
                text_->SetAnchoredPosition({ text_->GetAnchoredPosition().x, positionY });
            }
        })
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo)
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_bob");
}

void GameComponents::ResultButtonAnimationComponent::ApplyColor(const Vector4& color)
{
    if (!text_) {
        return;
    }
    text_->SetColor(color);
    // アウトラインは文字と別の色を持っている（既定は黒・不透明固定）。
    // フェード演出はここも一緒に動かさないと、文字が透明になった後もアウトラインの
    // 輪郭線だけ画面に残ってしまう
    Vector4 outline = text_->GetOutlineColor();
    outline.w = color.w;
    text_->SetOutline(outline, text_->GetOutlineWidth());
}

void GameComponents::ResultButtonAnimationComponent::StopTweens()
{
    Tween::KillById(tweenId_ + "_reaction");
    Tween::KillById(tweenId_ + "_confirm");
    Tween::KillById(tweenId_ + "_unselected_fade");
    Tween::KillById(tweenId_ + "_unselected_fade_color");
    Tween::KillById(tweenId_ + "_bob");
}

void GameComponents::ResultButtonAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

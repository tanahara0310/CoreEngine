#include "pch.h"
#include "TitleTextAnimationComponent.h"

#include "GameObject/GameObject.h"
#include "UI/UIText.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#endif
#include "Utility/Tween/Tween.h"

#include <utility>

using namespace CoreEngine;

namespace GameComponents
{
    CoreEngine::CVar<float> TitleTextAnimationComponent::FontSize{
        "Title.UI.HintFontSize",
        28.0f,
        "操作ヒントのフォントサイズ（ピクセル）",
        CoreEngine::CVarRange{ 8.0f, 128.0f } };

    CoreEngine::CVar<CoreEngine::Vector2> TitleTextAnimationComponent::Position{
        "Title.UI.HintPosition",
        { 0.0f, -80.0f },
        "画面下中央を基準にした操作ヒントの位置（ピクセル）",
        CoreEngine::CVarRange{ -2000.0f, 2000.0f } };

    CoreEngine::CVar<CoreEngine::Vector4> TitleTextAnimationComponent::Color{
        "Title.UI.HintColor",
        { 0.85f, 0.92f, 1.0f, 0.95f },
        "操作ヒントの色（RGBA）" };

    CoreEngine::CVar<int> TitleTextAnimationComponent::SortOrder{
        "Title.UI.HintSortOrder",
        1000,
        "操作ヒントの描画順",
        CoreEngine::CVarRange{ 0.0f, 5000.0f } };

    CoreEngine::CVar<float> TitleTextAnimationComponent::IntroDelay{
        "Title.UI.HintIntroDelay",
        0.75f,
        "操作ヒントを表示し始めるまでの遅延（秒）",
        CoreEngine::CVarRange{ 0.0f, 5.0f } };

    CoreEngine::CVar<float> TitleTextAnimationComponent::SlideDistance{
        "Title.UI.HintSlideDistance",
        12.0f,
        "操作ヒントが下からスライドする距離（ピクセル）",
        CoreEngine::CVarRange{ 0.0f, 200.0f } };

    CoreEngine::CVar<float> TitleTextAnimationComponent::IntroDuration{
        "Title.UI.HintIntroDuration",
        0.35f,
        "操作ヒントのフェード・スライド時間（秒）",
        CoreEngine::CVarRange{ 0.05f, 3.0f } };

    CoreEngine::CVar<float> TitleTextAnimationComponent::IdleScale{
        "Title.UI.HintIdleScale",
        1.025f,
        "操作ヒントの待機中に拡大する倍率",
        CoreEngine::CVarRange{ 1.0f, 1.2f } };

    CoreEngine::CVar<float> TitleTextAnimationComponent::IdleDuration{
        "Title.UI.HintIdleDuration",
        1.15f,
        "操作ヒントの待機中アニメーションの片道時間（秒）",
        CoreEngine::CVarRange{ 0.1f, 5.0f } };

    // スタート時のリアクション設定も、このアニメーションコンポーネント自身が所有する。
    CoreEngine::CVar<float> TitleTextAnimationComponent::StartReactionScale{
        "Title.UI.HintStartReactionScale",
        1.35f,
        "スタート時に操作ヒントを拡大する倍率",
        CoreEngine::CVarRange{ 1.0f, 3.0f } };

    CoreEngine::CVar<float> TitleTextAnimationComponent::StartReactionDuration{
        "Title.UI.HintStartReactionDuration",
        0.25f,
        "スタート時の操作ヒント拡大・縮小・フェード時間（秒）",
        CoreEngine::CVarRange{ 0.05f, 1.0f } };
}

#ifdef USE_IMGUI
bool GameComponents::TitleTextAnimationComponent::DrawInspector()
{
    const bool changed = CoreEngine::CVarUI::DrawTree("Title.UI");

    CoreEngine::UI::Separator();
    CoreEngine::UI::Hint(
        "ここで変更した値は操作ヒントの初期値です。"
        "位置・フォント・サイズは上のUITextインスペクターでも直接編集できます。");
    return changed;
}
#endif

void GameComponents::TitleTextAnimationComponent::Start()
{
    text_ = dynamic_cast<UIText*>(GetOwner());
    if (!text_) {
        SetEnabled(false);
        return;
    }

    baseFontSize_ = text_->GetFontSize();
    delay_ = IntroDelay.Get();
    slideDistance_ = SlideDistance.Get();
    duration_ = IntroDuration.Get();
    idleScale_ = IdleScale.Get();
    idleDuration_ = IdleDuration.Get();
    reactionScale_ = StartReactionScale.Get();
    reactionDuration_ = StartReactionDuration.Get();

    const Vector2 endPosition = text_->GetAnchoredPosition();
    const Vector4 endColor = text_->GetColor();

    Vector2 startPosition = endPosition;
    startPosition.y += slideDistance_;

    Vector4 startColor = endColor;
    startColor.w = 0.0f;

    text_->SetAnchoredPosition(startPosition);
    text_->SetColor(startColor);

    TweenSequence()
        .Append(
            Tween::To<Vector2>(
                startPosition,
                endPosition,
                duration_,
                [this](const Vector2& position) {
                    if (text_) {
                        text_->SetAnchoredPosition(position);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .Join(
            Tween::To<Vector4>(
                startColor,
                endColor,
                duration_,
                [this](const Vector4& color) {
                    if (text_) {
                        text_->SetColor(color);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .SetDelay(delay_)
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_)
        .OnComplete([this] {
            StartIdleAnimation();
            NotifyIntroComplete();
        });
}

void GameComponents::TitleTextAnimationComponent::NotifyIntroComplete()
{
    if (introCompleteNotified_) {
        return;
    }

    introCompleteNotified_ = true;
    if (onIntroComplete_) {
        onIntroComplete_();
    }
}

void GameComponents::TitleTextAnimationComponent::StartIdleAnimation()
{
    if (!text_) {
        return;
    }

    Tween::KillById(tweenId_ + "_idle");

    const float idleFontSize = baseFontSize_ * idleScale_;
    Tween::To<float>(
        baseFontSize_,
        idleFontSize,
        idleDuration_,
        [this](const float& fontSize) {
            if (text_) {
                text_->SetFontSize(fontSize);
            }
        })
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo)
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_idle");
}

void GameComponents::TitleTextAnimationComponent::PlayStartReaction(
    std::function<void()> onFinished)
{
    if (startReactionStarted_) {
        if (onFinished) {
            onFinished();
        }
        return;
    }
    startReactionStarted_ = true;

    Tween::KillById(tweenId_ + "_idle");

    if (!text_) {
        if (onFinished) {
            onFinished();
        }
        return;
    }

    // イントロがまだ残っていても、スタート入力時は反応アニメーションへ切り替える。
    // その場合も「このUIのイントロは終了した」として完了通知を失わない。
    Tween::KillById(tweenId_);
    NotifyIntroComplete();

    const float startFontSize = text_->GetFontSize();
    const float peakFontSize = baseFontSize_ * reactionScale_;
    const Vector4 startColor = text_->GetColor();
    Vector4 midColor = startColor;
    midColor.w *= 0.5f;
    Vector4 endColor = startColor;
    endColor.w = 0.0f;

    // 拡大してから縮小する山形のリアクションにし、αも両方の区間で
    // 少しずつ下げる。縮小区間の最後は元のサイズへ戻すことで、
    // 「一瞬大きくなった後に小さくなりながら消える」見え方になる。
    constexpr float kGrowRatio = 0.35f;
    const float growDuration = reactionDuration_ * kGrowRatio;
    const float shrinkDuration = reactionDuration_ - growDuration;

    TweenSequence reaction;
    reaction
        .Append(
            Tween::To<float>(
                startFontSize,
                peakFontSize,
                growDuration,
                [this](const float& fontSize) {
                    if (text_) {
                        text_->SetFontSize(fontSize);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .Join(
            Tween::To<Vector4>(
                startColor,
                midColor,
                growDuration,
                [this](const Vector4& color) {
                    if (text_) {
                        text_->SetColor(color);
                    }
                })
                .SetEase(EasingUtil::Type::EaseInCubic))
        .Append(
            Tween::To<float>(
                peakFontSize,
                baseFontSize_,
                shrinkDuration,
                [this](const float& fontSize) {
                    if (text_) {
                        text_->SetFontSize(fontSize);
                    }
                })
                .SetEase(EasingUtil::Type::EaseInCubic))
        .Join(
            Tween::To<Vector4>(
                midColor,
                endColor,
                shrinkDuration,
                [this](const Vector4& color) {
                    if (text_) {
                        text_->SetColor(color);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .AppendCallback(std::move(onFinished))
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(tweenId_ + "_start_reaction");
}

void GameComponents::TitleTextAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

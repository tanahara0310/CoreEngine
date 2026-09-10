#include "pch.h"
#include "GameStartPromptAnimationComponent.h"

#include "GameObject/GameObject.h"
#include "UI/UIText.h"
#include "Utility/Tween/Tween.h"
#include "WinApp/WinApp.h"

using namespace CoreEngine;

namespace
{
    constexpr float kEnterDuration = 0.55f;
    constexpr float kCenterHoldDuration = 2.0f;
    constexpr float kExitDuration = 0.65f;
    constexpr float kOffscreenMargin = 160.0f;
    constexpr const char* kTweenId = "game_start_distance_prompt";
}

void GameComponents::GameStartPromptAnimationComponent::Start()
{
    text_ = dynamic_cast<UIText*>(GetOwner());
    if (!text_) {
        SetEnabled(false);
        return;
    }

    const Vector2 centerPosition = text_->GetAnchoredPosition();
    const float offscreenDistance =
        static_cast<float>(WinApp::kReferenceWidth) * 0.5f + kOffscreenMargin;
    const Vector2 rightPosition{
        centerPosition.x + offscreenDistance,
        centerPosition.y };
    const Vector2 leftPosition{
        centerPosition.x - offscreenDistance,
        centerPosition.y };

    // 画面右外から入り、中央で2秒停止してから左外へ送り出す。
    text_->SetAnchoredPosition(rightPosition);
    TweenSequence()
        .Append(
            Tween::To<Vector2>(
                rightPosition,
                centerPosition,
                kEnterDuration,
                [this](const Vector2& position) {
                    if (text_) {
                        text_->SetAnchoredPosition(position);
                    }
                })
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .AppendInterval(kCenterHoldDuration)
        .Append(
            Tween::To<Vector2>(
                centerPosition,
                leftPosition,
                kExitDuration,
                [this](const Vector2& position) {
                    if (text_) {
                        text_->SetAnchoredPosition(position);
                    }
                })
                .SetEase(EasingUtil::Type::EaseInCubic))
        .SetLink(text_)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId(kTweenId);
}

void GameComponents::GameStartPromptAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

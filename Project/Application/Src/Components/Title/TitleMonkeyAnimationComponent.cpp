#include "pch.h"
#include "TitleMonkeyAnimationComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Components/Title/TitleLogoAnimationComponent.h"
#include "Components/Title/TitleTrolleyAnimationComponent.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#endif
#include "Utility/Tween/Tween.h"

using namespace CoreEngine;

namespace GameComponents
{
    CoreEngine::CVar<float> TitleMonkeyAnimationComponent::Distance{
        "Title.Monkey.Distance",
        1.0f,
        "トロッコの基準位置から monkey.obj の最終位置までの距離（Y方向）",
        CoreEngine::CVarRange{ 0.0f, 10.0f } };

    CoreEngine::CVar<CoreEngine::Vector3> TitleMonkeyAnimationComponent::Rotation{
        "Title.Monkey.Rotation",
        { 0.0f, 0.0f, 0.0f },
        "サルの最終回転（トロッコ基準、ラジアン）",
        CoreEngine::CVarRange{ -3.14f, 3.14f } };

    CoreEngine::CVar<float> TitleMonkeyAnimationComponent::IntroDuration{
        "Title.Monkey.IntroDuration",
        0.5f,
        "トロッコ到着後に monkey.obj が飛び出す時間（秒）",
        CoreEngine::CVarRange{ 0.05f, 3.0f } };

    CoreEngine::CVar<float> TitleMonkeyAnimationComponent::IntroOffset{
        "Title.Monkey.IntroOffset",
        1.5f,
        "monkey.obj をトロッコ内へ沈めておく距離（メートル）",
        CoreEngine::CVarRange{ 0.0f, 10.0f } };
}

#ifdef USE_IMGUI
bool GameComponents::TitleMonkeyAnimationComponent::DrawInspector()
{
    const bool changed = CoreEngine::CVarUI::DrawTree("Title.Monkey");

    CoreEngine::UI::Separator();
    CoreEngine::UI::Hint(
        "値はCVarとして保存されます。タイトルシーンを再読み込みすると"
        "monkeyの距離・回転・登場演出へ反映されます。");
    return changed;
}
#endif

void GameComponents::TitleMonkeyAnimationComponent::Start()
{
    GameObject* owner = GetOwner();
    transform_ = owner ? owner->GetComponent<TransformComponent>() : nullptr;
    if (!owner || !transform_) {
        SetEnabled(false);
        return;
    }

    basePosition_ = transform_->Get().translate;
    introDuration_ = IntroDuration.Get();
    introOffset_ = IntroOffset.Get();

    // トロッコの到着までは車体の中に沈めておき、到着後に飛び出させる。
    Vector3 startPosition = basePosition_;
    startPosition.y -= introOffset_;
    transform_->Get().translate = startPosition;

    const float introDelay =
        TitleLogoAnimationComponent::IntroDuration.Get()
        + TitleTrolleyAnimationComponent::IntroDelay.Get()
        + TitleTrolleyAnimationComponent::IntroDuration.Get();

    Tween::To<float>(
        &transform_->Get().translate.y,
        basePosition_.y,
        introDuration_)
        .SetEase(EasingUtil::Type::EaseOutBack)
        .SetDelay(introDelay)
        .SetLink(owner)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_monkey_intro")
        .OnComplete([this] {
            if (!introCompleteNotified_) {
                introCompleteNotified_ = true;
                if (onIntroComplete_) {
                    onIntroComplete_();
                }
            }
        });
}

void GameComponents::TitleMonkeyAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

#include "pch.h"
#include "TitleTrolleyAnimationComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Components/Title/TitleLogoAnimationComponent.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#endif
#include "Utility/Tween/Tween.h"

using namespace CoreEngine;

namespace GameComponents
{
    CoreEngine::CVar<CoreEngine::Vector3> TitleTrolleyAnimationComponent::Position{
        "Title.Trolley.Position",
        { 0.0f, 0.0f, -0.5f },
        "トロッコの最終位置（ワールド座標）",
        CoreEngine::CVarRange{ -10.0f, 10.0f } };

    CoreEngine::CVar<CoreEngine::Vector3> TitleTrolleyAnimationComponent::Rotation{
        "Title.Trolley.Rotation",
        { 0.0f, 0.0f, 0.0f },
        "トロッコの最終回転（ラジアン）",
        CoreEngine::CVarRange{ -3.14f, 3.14f } };

    CoreEngine::CVar<float> TitleTrolleyAnimationComponent::IntroDelay{
        "Title.Trolley.IntroDelay",
        0.35f,
        "タイトルの落下完了後、トロッコが登場するまでの待ち時間（秒）",
        CoreEngine::CVarRange{ 0.0f, 5.0f } };

    CoreEngine::CVar<float> TitleTrolleyAnimationComponent::IntroDuration{
        "Title.Trolley.IntroDuration",
        0.9f,
        "トロッコが画面下から登場する時間（秒）",
        CoreEngine::CVarRange{ 0.05f, 5.0f } };

    CoreEngine::CVar<float> TitleTrolleyAnimationComponent::IntroOffset{
        "Title.Trolley.IntroOffset",
        8.0f,
        "トロッコを最終位置より下へ離す距離（メートル）",
        CoreEngine::CVarRange{ 0.0f, 30.0f } };

    CoreEngine::CVar<CoreEngine::Vector3> TitleTrolleyAnimationComponent::BobStart{
        "Title.Trolley.BobStart",
        { 0.0f, 0.0f, 0.0f },
        "浮遊の線形補間を開始する位置オフセット（トロッコ基準）",
        CoreEngine::CVarRange{ -10.0f, 10.0f } };

    CoreEngine::CVar<CoreEngine::Vector3> TitleTrolleyAnimationComponent::BobEnd{
        "Title.Trolley.BobEnd",
        { 0.0f, 0.12f, 0.0f },
        "浮遊の線形補間を終了する位置オフセット（トロッコ基準）",
        CoreEngine::CVarRange{ -10.0f, 10.0f } };

    CoreEngine::CVar<float> TitleTrolleyAnimationComponent::BobDuration{
        "Title.Trolley.BobDuration",
        1.6f,
        "トロッコが上下へ浮遊する片道の時間（秒）",
        CoreEngine::CVarRange{ 0.1f, 10.0f } };
}

#ifdef USE_IMGUI
bool GameComponents::TitleTrolleyAnimationComponent::DrawInspector()
{
    const bool changed = CoreEngine::CVarUI::DrawTree("Title.Trolley");

    CoreEngine::UI::Separator();
    CoreEngine::UI::Hint(
        "値はCVarとして保存されます。タイトルシーンを再読み込みすると"
        "トロッコの配置・回転・登場演出へ反映されます。サルの設定はmonkeyオブジェクト側にあります。");
    return changed;
}
#endif

void GameComponents::TitleTrolleyAnimationComponent::Start()
{
    GameObject* owner = GetOwner();
    transform_ = owner ? owner->GetComponent<TransformComponent>() : nullptr;
    if (!owner || !transform_) {
        SetEnabled(false);
        return;
    }

    basePosition_ = Position.Get();
    introDuration_ = IntroDuration.Get();
    introOffset_ = IntroOffset.Get();
    bobStart_ = BobStart.Get();
    bobEnd_ = BobEnd.Get();
    bobDuration_ = BobDuration.Get();
    const float introDelay =
        TitleLogoAnimationComponent::IntroDuration.Get()
        + IntroDelay.Get();

    // 最終位置から下へ離した地点を開始位置にする。既定値ではカメラの画面外から
    // タイトルの落下が終わって少し間を置いたあと、EaseOutCubic で減速しながら
    // 所定位置へ収まる。
    Vector3 startPosition = basePosition_;
    startPosition.y -= introOffset_;
    transform_->Get().translate = startPosition;

    Tween::To<float>(
        &transform_->Get().translate.y,
        basePosition_.y,
        introDuration_)
        .SetEase(EasingUtil::Type::EaseOutCubic)
        .SetDelay(introDelay)
        .SetLink(owner)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_trolley_intro")
        .OnComplete([this] {
            StartIdleAnimation();
            if (!introCompleteNotified_) {
                introCompleteNotified_ = true;
                if (onIntroComplete_) {
                    onIntroComplete_();
                }
            }
        });
}

void GameComponents::TitleTrolleyAnimationComponent::StartIdleAnimation()
{
    if (!GetOwner() || !transform_) {
        return;
    }

    // タイトルロゴと同じく、登場後はトロッコを基準にした始点・終点を
    // Vector3 の線形補間で往復する。初期値は X/Z を 0 にしているため、
    // トロッコと、その子である monkey の真上を通る上下移動になる。
    const Vector3 bobStartPosition = basePosition_ + bobStart_;
    const Vector3 bobEndPosition = basePosition_ + bobEnd_;
    transform_->Get().translate = bobStartPosition;

    Tween::To<Vector3>(
        &transform_->Get().translate,
        bobEndPosition,
        bobDuration_)
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo)
        .SetLink(GetOwner())
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_trolley_bob");
}

void GameComponents::TitleTrolleyAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

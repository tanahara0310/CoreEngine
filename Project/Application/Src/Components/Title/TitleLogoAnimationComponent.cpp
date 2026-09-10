#include "pch.h"
#include "TitleLogoAnimationComponent.h"

#include "Audio/AudioSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakePresets.h"
#include "Components/Title/TitleCameraShakeSettingsComponent.h"
#include "EngineSystem/EngineSystem.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#endif
#include "Utility/Tween/Tween.h"

using namespace CoreEngine;

namespace
{
    constexpr const char* kTitleBoundSePath = "Sounds/SE/title_bound.mp3";
}

namespace GameComponents
{
    CoreEngine::CVar<CoreEngine::Vector3> TitleLogoAnimationComponent::Position{
        "Title.Transform.Position",
        { -0.08570337f, 3.187f, -0.81899166f },
        "title.obj の最終位置（ワールド座標）",
        CoreEngine::CVarRange{ -10.0f, 10.0f } };

    CoreEngine::CVar<CoreEngine::Vector3> TitleLogoAnimationComponent::Rotation{
        "Title.Transform.Rotation",
        { 0.0f, 0.0f, 0.0f },
        "title.obj の最終回転（ラジアン）",
        CoreEngine::CVarRange{ -3.14f, 3.14f } };

    CoreEngine::CVar<CoreEngine::Vector3> TitleLogoAnimationComponent::Scale{
        "Title.Transform.Scale",
        { 1.0f, 1.0f, 1.0f },
        "title.obj の最終スケール",
        CoreEngine::CVarRange{ 0.01f, 10.0f } };

    CoreEngine::CVar<float> TitleLogoAnimationComponent::IntroDuration{
        "Title.Animation.IntroDuration",
        1.05f,
        "ロゴが落下して着地するまでの時間（秒）",
        CoreEngine::CVarRange{ 0.05f, 5.0f } };

    CoreEngine::CVar<float> TitleLogoAnimationComponent::IntroScale{
        "Title.Animation.IntroScale",
        0.82f,
        "落下開始時のロゴのスケール（最終スケールに対する倍率）",
        CoreEngine::CVarRange{ 0.1f, 1.0f } };

    CoreEngine::CVar<float> TitleLogoAnimationComponent::DropHeight{
        "Title.Animation.DropHeight",
        3.5f,
        "ロゴを最終位置より上へ離す高さ（メートル）",
        CoreEngine::CVarRange{ 0.0f, 20.0f } };

    CoreEngine::CVar<float> TitleLogoAnimationComponent::BobHeight{
        "Title.Animation.BobHeight",
        0.12f,
        "着地後に上下へ浮遊する高さ（メートル）",
        CoreEngine::CVarRange{ 0.0f, 2.0f } };

    CoreEngine::CVar<float> TitleLogoAnimationComponent::BobDuration{
        "Title.Animation.BobDuration",
        1.6f,
        "着地後の上下浮遊が片道にかかる時間（秒）",
        CoreEngine::CVarRange{ 0.1f, 10.0f } };

    CoreEngine::CVar<float> TitleLogoAnimationComponent::RotationAmplitude{
        "Title.Animation.RotationAmplitude",
        0.035f,
        "落下時・待機時の左右回転幅（ラジアン）",
        CoreEngine::CVarRange{ 0.0f, 1.0f } };
}

#ifdef USE_IMGUI
bool GameComponents::TitleLogoAnimationComponent::DrawInspector()
{
    bool changed = CoreEngine::CVarUI::DrawTree("Title.Transform");
    changed |= CoreEngine::CVarUI::DrawTree("Title.Animation");

    CoreEngine::UI::Separator();
    CoreEngine::UI::Hint(
        "値はCVarとして保存されます。タイトルシーンを再読み込みすると"
        "生成時のモデル姿勢・アニメーションへ反映されます。カメラシェイク設定は"
        "TitleCameraShakeSettingsを選択してください。");
    return changed;
}
#endif

void GameComponents::TitleLogoAnimationComponent::Start()
{
    GameObject* owner = GetOwner();
    transform_ = owner ? owner->GetComponent<TransformComponent>() : nullptr;
    if (!owner || !transform_) {
        SetEnabled(false);
        return;
    }

    const WorldTransform& transform = transform_->Get();
    basePosition_ = transform.translate;
    baseRotation_ = transform.rotate;
    baseScale_ = transform.scale;

    // アニメーションの値はタイトルオブジェクト生成時に CVar から読み取る。
    // そのため、ハードコードされた setter 呼び出しをシーン側へ並べず、
    // インスペクターで保存した値を次回のタイトル表示へ反映できる。
    introDuration_ = IntroDuration.Get();
    introScale_ = IntroScale.Get();
    dropHeight_ = DropHeight.Get();
    bobHeight_ = BobHeight.Get();
    bobDuration_ = BobDuration.Get();
    rotationAmplitude_ = RotationAmplitude.Get();
    shakeStrength_ = TitleCameraShakeSettingsComponent::ShakeStrength.Get();
    nextBounceIndex_ = 0;

    // シーン側で設定された最終姿勢を保存する。位置は最終地点より上へ移し、
    // そこから落下させる。EaseOutBounce は「速く落ちる → 地面で大きく跳ねる
    // → 小さく2～3回反発して停止」というカーブなので、別の物理システムを
    // 用意しなくてもタイトルロゴらしい着地を作れる。
    Vector3 dropStartPosition = basePosition_;
    dropStartPosition.y += dropHeight_;
    transform_->Get().translate = dropStartPosition;

    // 落下開始時は少し小さくし、わずかに傾ける。落下の勢いが加わることで、
    // 静的に表示されるよりも画面へ入ってくる方向が分かりやすくなる。
    transform_->Get().scale = {
        baseScale_.x * introScale_,
        baseScale_.y * introScale_,
        baseScale_.z * introScale_,
    };

    Vector3 introRotation = baseRotation_;
    introRotation.y -= rotationAmplitude_;
    transform_->Get().rotate = introRotation;

    TweenSequence()
        .Append(
            Tween::To<float>(
                &transform_->Get().translate.y,
                basePosition_.y,
                introDuration_)
                .SetEase(EasingUtil::Type::EaseOutBounce)
                .OnUpdate([this](float progress) { OnIntroProgress(progress); }))
        .Join(
            Tween::ScaleTo(owner, baseScale_, introDuration_)
                .SetEase(EasingUtil::Type::EaseOutBack))
        .Join(
            Tween::RotateTo(owner, baseRotation_, introDuration_)
                .SetEase(EasingUtil::Type::EaseOutCubic))
        .AppendCallback([this] {
            StartIdleAnimation();
            if (!introCompleteNotified_) {
                introCompleteNotified_ = true;
                if (onIntroComplete_) {
                    onIntroComplete_();
                }
            }
        })
        .SetLink(owner)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_logo_intro");
}

void GameComponents::TitleLogoAnimationComponent::OnIntroProgress(float progress)
{
    // EasingUtil::EaseOutBounce の d1=2.75 に対応する接地位置。進捗は Tween の
    // 生の時間進捗なので、フレームレートに関係なく接地を一度ずつ拾える。
    static constexpr float kBounceProgress[] = {
        1.0f / 2.75f,
        2.0f / 2.75f,
        2.5f / 2.75f,
        1.0f,
    };
    static constexpr float kBounceIntensity[] = { 1.0f, 0.55f, 0.3f, 0.18f };

    while (nextBounceIndex_ < 4
        && progress >= kBounceProgress[nextBounceIndex_]) {
        PlayBounceShake(kBounceIntensity[nextBounceIndex_]);
        ++nextBounceIndex_;
    }
}

void GameComponents::TitleLogoAnimationComponent::PlayBounceShake(float intensity)
{
    CoreEngine::CameraShakeParams params = CoreEngine::CameraShakePresets::Landing();
    const float totalIntensity = intensity * shakeStrength_;
    params.positionAmplitude *= totalIntensity;
    params.rotationAmplitude *= totalIntensity;
    CoreEngine::CameraShake::Play(params);

    if (GameObject* owner = GetOwner()) {
        if (EngineSystem* engine = owner->GetEngineSystem()) {
            if (AudioSystem* audioSystem = engine->GetService<AudioSystem>()) {
                audioSystem->PlayOneShot(
                    kTitleBoundSePath,
                    { .bus = AudioBus::SE, .volume = intensity });
            }
        }
    }
}

void GameComponents::TitleLogoAnimationComponent::StartIdleAnimation()
{
    GameObject* owner = GetOwner();
    if (!owner || !transform_) {
        return;
    }

    // 2 本の Tween が同じ値を同時に書き換えないよう、軸を分けている。
    Tween::To<float>(
        &transform_->Get().translate.y,
        basePosition_.y + bobHeight_,
        bobDuration_)
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo)
        .SetLink(owner)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_logo_bob");

    Tween::To<float>(
        &transform_->Get().rotate.y,
        baseRotation_.y + rotationAmplitude_,
        bobDuration_ * 1.25f)
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo)
        .SetLink(owner)
        .SetUpdateType(TweenUpdate::Unscaled)
        .SetId("title_logo_sway");
}

void GameComponents::TitleLogoAnimationComponent::OnDestroy()
{
    if (GetOwner()) {
        Tween::KillByLink(GetOwner());
    }
}

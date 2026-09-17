#include "pch.h"
#include "CameraShowcaseComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Scene/SceneManager.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <utility>

using namespace CoreEngine;

REFLECT_REGISTER(CameraShowcaseComponent)

void CameraShowcaseComponent::Configure(std::vector<Shot> shots, ApplyShotFunc applyShot,
    IsGameCameraActiveFunc isGameCameraActive)
{
    shots_ = std::move(shots);
    applyShot_ = std::move(applyShot);
    isGameCameraActive_ = std::move(isGameCameraActive);
    currentIndex_ = ClampedStartIndex();
    ApplyCurrentShot();
}

void CameraShowcaseComponent::Start()
{
    GameObject* const owner = GetOwner();
    EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
    if (auto* postEffectManager = engine ? engine->GetService<PostEffectManager>() : nullptr) {
        fadeEffect_ = postEffectManager->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);
    }
    if (fadeEffect_) {
        fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);
    }

    // 最初のカットも、ループ中のカットと同じく黒から明ける
    currentIndex_ = ClampedStartIndex();
    phase_ = Phase::FadeIn;
    timer_ = 0.0f;
    wasCycling_ = cycling_;
    suspended_ = !IsGameCameraActive();
    started_ = true;
    ApplyCurrentShot();
    ApplyFadeAlpha((suspended_ || !cycling_) ? 0.0f : 1.0f);
}

void CameraShowcaseComponent::Update()
{
    if (!started_ || shots_.empty()) {
        return;
    }

    // 巡回を切ったら、今見えているカットのまま止める
    if (!cycling_) {
        if (wasCycling_) {
            ApplyFadeAlpha(0.0f);
            wasCycling_ = false;
        }
        return;
    }
    if (!wasCycling_) {
        // 再開はフェードインから
        phase_ = Phase::FadeIn;
        timer_ = 0.0f;
        wasCycling_ = true;
    }

    // エディタのカメラで覗いている間は、フェードを畳んで止める（段階と経過時間は残す）
    if (!IsGameCameraActive()) {
        if (!suspended_) {
            ApplyFadeAlpha(0.0f);
            suspended_ = true;
        }
        return;
    }
    if (suspended_) {
        // 止めた時点の濃さへ戻してから続ける
        suspended_ = false;
        ApplyFadeAlpha(CurrentPhaseAlpha());
    }

    const float fadeDuration = std::max(fadeSeconds_, 0.01f);
    const float holdDuration = std::max(holdSeconds_, 0.01f);

    timer_ += Time::UnscaledDeltaTime();

    switch (phase_) {
    case Phase::FadeIn: {
        const float t = std::clamp(timer_ / fadeDuration, 0.0f, 1.0f);
        ApplyFadeAlpha(1.0f - t);
        if (t >= 1.0f) {
            phase_ = Phase::Hold;
            timer_ = 0.0f;
        }
        break;
    }
    case Phase::Hold: {
        ApplyFadeAlpha(0.0f);
        if (timer_ >= holdDuration) {
            phase_ = Phase::FadeOut;
            timer_ = 0.0f;
        }
        break;
    }
    case Phase::FadeOut: {
        const float t = std::clamp(timer_ / fadeDuration, 0.0f, 1.0f);
        ApplyFadeAlpha(t);
        if (t >= 1.0f) {
            // 完全に暗転したフレームでだけ構図を差し替える
            currentIndex_ = (currentIndex_ + 1) % shots_.size();
            ApplyCurrentShot();
            phase_ = Phase::Black;
            timer_ = 0.0f;
        }
        break;
    }
    case Phase::Black: {
        // 差し替えの直後は、TAA・自動露出・水面の履歴が追い付くまで黒を保つ
        ApplyFadeAlpha(1.0f);
        if (timer_ >= std::max(blackSeconds_, 0.0f)) {
            phase_ = Phase::FadeIn;
            timer_ = 0.0f;
        }
        break;
    }
    }
}

void CameraShowcaseComponent::OnDestroy()
{
    // シーン遷移のフェード中は、フェードを遷移に任せる
    GameObject* const owner = GetOwner();
    EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
    SceneManager* const sceneManager = engine ? engine->GetSceneManager() : nullptr;
    const bool keepFade = sceneManager && sceneManager->IsTransitioning();
    if (fadeEffect_ && !keepFade) {
        ApplyFadeAlpha(0.0f);
        fadeEffect_->SetEnabled(false);
    }
    fadeEffect_ = nullptr;
    applyShot_ = nullptr;
    isGameCameraActive_ = nullptr;
    shots_.clear();
}

void CameraShowcaseComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
{
    // 上書き構図は、切り替えた・動かしたときにすぐ当てる
    if (property.name.rfind("override", 0) == 0) {
        ApplyCurrentShot();
        return;
    }
    // 再生前は、最初のカットを変えたらそのカットを見せる
    if (property.name == "startIndex" && !started_) {
        currentIndex_ = ClampedStartIndex();
        ApplyCurrentShot();
    }
}

void CameraShowcaseComponent::ApplyCurrentShot()
{
    if (!applyShot_) {
        return;
    }
    if (overrideShot_) {
        applyShot_(Shot{ overridePosition_, overrideRotation_, overrideFovDegrees_, overrideFarClip_ });
        return;
    }
    if (currentIndex_ < shots_.size()) {
        applyShot_(shots_[currentIndex_]);
    }
}

void CameraShowcaseComponent::ApplyFadeAlpha(float alpha)
{
    if (!fadeEffect_) {
        return;
    }
    // ほぼ透明ならパスごと切る
    const bool needsFade = alpha > 0.001f;
    fadeEffect_->SetEnabled(needsFade);
    fadeEffect_->SetFadeAlpha(alpha);
}

float CameraShowcaseComponent::CurrentPhaseAlpha() const
{
    const float fadeDuration = std::max(fadeSeconds_, 0.01f);
    switch (phase_) {
    case Phase::FadeIn:
        return 1.0f - std::clamp(timer_ / fadeDuration, 0.0f, 1.0f);
    case Phase::FadeOut:
        return std::clamp(timer_ / fadeDuration, 0.0f, 1.0f);
    case Phase::Black:
        return 1.0f;
    case Phase::Hold:
    default:
        return 0.0f;
    }
}

bool CameraShowcaseComponent::IsGameCameraActive() const
{
    return !isGameCameraActive_ || isGameCameraActive_();
}

std::size_t CameraShowcaseComponent::ClampedStartIndex() const
{
    if (shots_.empty()) {
        return 0;
    }
    return static_cast<std::size_t>(std::clamp(startIndex_, 0, static_cast<int>(shots_.size()) - 1));
}

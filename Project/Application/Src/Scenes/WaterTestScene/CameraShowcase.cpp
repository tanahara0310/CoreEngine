#include "pch.h"
#include "CameraShowcase.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Utility/CVar/CVar.h"

#include <algorithm>

using namespace CoreEngine;

namespace {
    // 演出のテンポは実行中に詰めたくなるので CVar にしておく（ImGui と保存が自動で付く）。
    // フェードの進行度そのものは実行時状態なので CVar にしない（黒画面で起動する事故になる）。
    CVar<bool> cvEnabled{
        "app.CameraShowcase.Enabled", true,
        "起動カメラのカット巡回演出を有効にする" };
    CVar<float> cvHoldSeconds{
        "app.CameraShowcase.HoldSeconds", 6.0f,
        "1 カットを見せている時間 [秒]", CVarRange{ 0.5f, 60.0f } };
    CVar<float> cvFadeSeconds{
        "app.CameraShowcase.FadeSeconds", 1.2f,
        "フェードイン／フェードアウトの時間 [秒]", CVarRange{ 0.1f, 10.0f } };
    CVar<float> cvBlackSeconds{
        "app.CameraShowcase.BlackSeconds", 0.4f,
        "カットの切れ目で完全暗転を保つ時間 [秒]", CVarRange{ 0.0f, 5.0f } };
}

void CameraShowcase::Initialize(EngineSystem* engine, std::vector<Shot> shots, ApplyShotFunc applyShot,
    IsReleaseCameraActiveFunc isReleaseCameraActive)
{
    engine_ = engine;
    shots_ = std::move(shots);
    applyShot_ = std::move(applyShot);
    isReleaseCameraActive_ = std::move(isReleaseCameraActive);
    currentIndex_ = 0;
    timer_ = 0.0f;
    // 起動直後は黒から明ける。1 カット目もフェードインで始めることで、
    // ループ中のどのカットとも同じ入り方になる
    phase_ = Phase::FadeIn;
    wasEnabled_ = cvEnabled.Get();
    suspended_ = !IsReleaseCameraActive();

    if (auto* postEffectManager = engine_ ? engine_->GetService<PostEffectManager>() : nullptr) {
        fadeEffect_ = postEffectManager->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);
    }
    if (fadeEffect_) {
        fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);
    }

    ApplyCurrentShot();
    // デバッグ視点で開始した場合まで黒を置くと、エディタの画が理由もなく暗転する
    ApplyFadeAlpha(suspended_ ? 0.0f : 1.0f);
}

void CameraShowcase::Update(float deltaTime)
{
    if (shots_.empty()) {
        return;
    }

    // 演出を切ったら、今見えているカットをそのまま静止画として残す（暗転したままにしない）
    if (!cvEnabled.Get()) {
        if (wasEnabled_) {
            ApplyFadeAlpha(0.0f);
            wasEnabled_ = false;
        }
        return;
    }
    if (!wasEnabled_) {
        // 再開はフェードインから。切った瞬間の明るさから続けると不連続になる
        phase_ = Phase::FadeIn;
        timer_ = 0.0f;
        wasEnabled_ = true;
    }

    // デバッグ（エディタ）視点で覗いている間は演出を止める。
    // フェードはリリースカメラの見せ方であって、自由に見回している視界を暗くする理由がない。
    // phase_ / timer_ には手を付けないので、リリースカメラへ戻せば止めた続きから再開する。
    if (!IsReleaseCameraActive()) {
        if (!suspended_) {
            ApplyFadeAlpha(0.0f);
            suspended_ = true;
        }
        return;
    }
    if (suspended_) {
        // 中断のあいだ畳んでいたフェードを、止めた時点の濃さへ戻してから進行を再開する
        suspended_ = false;
        ApplyFadeAlpha(CurrentPhaseAlpha());
    }

    const float fadeDuration = std::max(cvFadeSeconds.Get(), 0.01f);
    const float holdDuration = std::max(cvHoldSeconds.Get(), 0.01f);

    timer_ += deltaTime;

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
            // 完全暗転しきったこのフレームでだけ構図を差し替える。
            // カメラが飛ぶ瞬間は真っ黒なので、視点のワープも水面の履歴（TAA）の乱れも画に出ない
            currentIndex_ = (currentIndex_ + 1) % shots_.size();
            ApplyCurrentShot();
            phase_ = Phase::Black;
            timer_ = 0.0f;
        }
        break;
    }
    case Phase::Black: {
        // 黒を数フレーム保つ。切り替え直後は TAA・自動露出・水面の履歴が新しい視点に
        // 追従しきっておらず、ここを飛ばすとフェードインの頭に一瞬だけ破綻した画が出る
        ApplyFadeAlpha(1.0f);
        if (timer_ >= std::max(cvBlackSeconds.Get(), 0.0f)) {
            phase_ = Phase::FadeIn;
            timer_ = 0.0f;
        }
        break;
    }
    }
}

void CameraShowcase::Shutdown(bool keepFade)
{
    // シーン遷移のフェード中に横から alpha=0 を書くと、暗転しているはずの画が一瞬戻る。
    // その場合はフェードの主導権を SceneTransition へ返すだけにする
    if (!keepFade) {
        ApplyFadeAlpha(0.0f);
        if (fadeEffect_) {
            fadeEffect_->SetEnabled(false);
        }
    }
    fadeEffect_ = nullptr;
    engine_ = nullptr;
    applyShot_ = nullptr;
    isReleaseCameraActive_ = nullptr;
    suspended_ = false;
    shots_.clear();
}

void CameraShowcase::ApplyCurrentShot()
{
    if (!applyShot_ || currentIndex_ >= shots_.size()) {
        return;
    }
    applyShot_(shots_[currentIndex_]);
}

void CameraShowcase::ApplyFadeAlpha(float alpha)
{
    if (!fadeEffect_) {
        return;
    }
    // 完全に透明なときはパスごと切る（毎フレーム無駄にディスパッチしない）
    const bool needsFade = alpha > 0.001f;
    fadeEffect_->SetEnabled(needsFade);
    fadeEffect_->SetFadeAlpha(alpha);
}

float CameraShowcase::CurrentPhaseAlpha() const
{
    const float fadeDuration = std::max(cvFadeSeconds.Get(), 0.01f);
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

bool CameraShowcase::IsReleaseCameraActive() const
{
    // 判定が渡されていないシーンでは、従来どおり常に演出を回す
    return !isReleaseCameraActive_ || isReleaseCameraActive_();
}

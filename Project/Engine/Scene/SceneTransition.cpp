#include "SceneTransition.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/PostEffect/PostEffectManager.h"
#include "Engine/Graphics/PostEffect/Effect/FadeEffect.h"
#include "Engine/Graphics/PostEffect/PostEffectNames.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Audio/SoundManager.h"


namespace CoreEngine
{
void SceneTransition::Initialize(EngineSystem* engine) {
engine_ = engine;

// PostEffectManagerを取得
postEffectManager_ = engine_->GetComponent<PostEffectManager>();

// FadeEffectを取得
fadeEffect_ = postEffectManager_->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);

// SoundManagerを取得
soundManager_ = engine_->GetComponent<SoundManager>();

// 初期状態：完全に透明（フェードなし）
fadeEffect_->SetFadeAlpha(0.0f);
fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);
fadeEffect_->SetEnabled(false); // デフォルトは無効

// 初期状態
phase_ = TransitionPhase::Idle;
    timer_ = 0.0f;
    duration_ = 1.0f;
}

void SceneTransition::Update(float deltaTime) {
    if (!fadeEffect_) {
        return;
    }

    if (phase_ == TransitionPhase::Idle) {
        return;
    }

    // タイマー更新
    timer_ += deltaTime;

    switch (phase_) {
    case TransitionPhase::FadeOut:
        // フェードアウト完了チェック
        if (timer_ >= duration_) {
            timer_ = duration_;
            // 完全暗転後、数フレーム待機してからChangingフェーズに移行
            waitFrameCounter_++;
            if (waitFrameCounter_ >= kWaitFramesAfterFadeOut) {
                phase_ = TransitionPhase::Changing;
            }
        }
        break;

    case TransitionPhase::FadeIn:
        // フェードイン完了チェック
        if (timer_ >= duration_) {
            timer_ = 0.0f;
            phase_ = TransitionPhase::Idle;
            fadeEffect_->SetFadeAlpha(0.0f);
            fadeEffect_->SetEnabled(false); // フェード完了後は無効化
        }
        break;

    case TransitionPhase::Changing:
        // シーン切り替え待機中（完全に黒のまま維持）
        break;

    default:
        break;
    }

    // フェードエフェクトにアルファ値を適用
    ApplyFadeToPostEffect();

    // BGM音量を適用（フェードと同期）
    ApplyBGMVolume();
}

void SceneTransition::StartTransition(TransitionType type, float duration) {
    if (!fadeEffect_) {
        return;
    }

    type_ = type;
    duration_ = duration;
    timer_ = 0.0f;
    waitFrameCounter_ = 0;

    if (type_ == TransitionType::None) {
        // トランジション無し → 即座に切り替え準備完了
        phase_ = TransitionPhase::Changing;
        fadeEffect_->SetEnabled(false);
    } else {
        // フェードアウト開始
        phase_ = TransitionPhase::FadeOut;
        fadeEffect_->SetEnabled(true); // フェード開始時に有効化
        fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);
    }
}

bool SceneTransition::IsReadyToChangeScene() const {
    return phase_ == TransitionPhase::Changing;
}

void SceneTransition::OnSceneChanged() {
    if (!fadeEffect_) {
        return;
    }

    if (type_ == TransitionType::None) {
        // トランジション無し → 即座に待機状態へ
        phase_ = TransitionPhase::Idle;
        timer_ = 0.0f;
        waitFrameCounter_ = 0;
        fadeEffect_->SetFadeAlpha(0.0f);
        fadeEffect_->SetEnabled(false);
    } else {
        // フェードイン開始
        phase_ = TransitionPhase::FadeIn;
        timer_ = 0.0f;
        waitFrameCounter_ = 0;
        fadeEffect_->SetEnabled(true);
    }
}

bool SceneTransition::IsTransitioning() const {
    return phase_ != TransitionPhase::Idle;
}

bool SceneTransition::IsBlocking() const {
    // フェードアウト中とChanging中はシーン更新をブロック
    return phase_ == TransitionPhase::FadeOut || phase_ == TransitionPhase::Changing;
}

void SceneTransition::SkipTransition() {
    if (!fadeEffect_) {
        return;
    }

    phase_ = TransitionPhase::Idle;
    timer_ = 0.0f;
    waitFrameCounter_ = 0;
    fadeEffect_->SetFadeAlpha(0.0f);
    fadeEffect_->SetEnabled(false);
}

float SceneTransition::CalculateFadeAlpha() const {
    if (phase_ == TransitionPhase::Idle) {
        return 0.0f;
    }

    if (phase_ == TransitionPhase::Changing) {
        return 1.0f; // 完全に黒
    }

    float t = timer_ / duration_;
    t = std::clamp(t, 0.0f, 1.0f);

    switch (phase_) {
    case TransitionPhase::FadeOut:
        // 0.0 → 1.0（徐々に暗くなる）
        return t;

    case TransitionPhase::FadeIn:
        // 1.0 → 0.0（徐々に明るくなる）
        return 1.0f - t;

    default:
        return 0.0f;
    }
}

void SceneTransition::ApplyFadeToPostEffect() {
    if (!fadeEffect_) {
        return;
    }

    float alpha = CalculateFadeAlpha();
    fadeEffect_->SetFadeAlpha(alpha);
}

void SceneTransition::SetBGMVolumeCallback(std::function<void(float)> callback) {
    bgmVolumeCallback_ = callback;
}

void SceneTransition::ClearBGMVolumeCallback() {
    bgmVolumeCallback_ = nullptr;
}

void SceneTransition::ApplyBGMVolume() {
    if (!bgmVolumeCallback_) {
        return;
    }

    // フェードアルファ値を取得（0.0 = 透明, 1.0 = 不透明）
    float fadeAlpha = CalculateFadeAlpha();

    // フェードフェーズに応じてBGM音量を調整
    float volumeMultiplier = 1.0f;

    switch (phase_) {
    case TransitionPhase::FadeOut:
        // フェードアウト中：音量を徐々に下げる（1.0 → 0.0）
        volumeMultiplier = 1.0f - fadeAlpha;
        break;

    case TransitionPhase::Changing:
        // シーン切替中：完全に無音
        volumeMultiplier = 0.0f;
        break;

    case TransitionPhase::FadeIn:
        // フェードイン中：音量を徐々に上げる（0.0 → 1.0）
        volumeMultiplier = 1.0f - fadeAlpha;
        break;

    case TransitionPhase::Idle:
    default:
        // 待機中：通常音量
        volumeMultiplier = 1.0f;
        break;
    }

    // コールバックを呼び出して音量倍率を通知
    bgmVolumeCallback_(volumeMultiplier);
}
}

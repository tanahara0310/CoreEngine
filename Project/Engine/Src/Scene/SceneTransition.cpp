#include "pch.h"
#include "SceneTransition.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
#include "Graphics/PostEffect/Effect/LoadingScreen/LoadingScreenEffect.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Audio/SoundManager.h"
#include "Utility/CVar/CVar.h"


namespace CoreEngine
{
namespace
{
    CVar<float> cvMinSeconds{
        "r.Loading.MinSeconds", 1.5f,
        "ローディング画面を最低限表示し続ける秒数",
        CVarRange{ 0.0f, 5.0f } };

    CVar<float> cvGaugeAfterSeconds{
        "r.Loading.GaugeAfterSeconds", 3.0f,
        "進捗ゲージを出し始めるまでの秒数（負値でゲージ無効）",
        CVarRange{ -1.0f, 10.0f } };
}

void SceneTransition::Initialize(EngineSystem* engine) {
engine_ = engine;

// PostEffectManagerを取得
postEffectManager_ = engine_->GetService<PostEffectManager>();

// FadeEffectを取得
fadeEffect_ = postEffectManager_->GetEffect<FadeEffect>(PostEffectNames::FadeEffect);

// ローディング画面エフェクトを取得
loadingScreenEffect_ = postEffectManager_->GetEffect<LoadingScreenEffect>(PostEffectNames::LoadingScreen);

// SoundManagerを取得
soundManager_ = engine_->GetService<SoundManager>();

// 初期状態：完全に透明（フェードなし）
fadeEffect_->SetFadeAlpha(0.0f);
fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);
fadeEffect_->SetEnabled(false); // デフォルトは無効

if (loadingScreenEffect_) {
    loadingScreenEffect_->SetScreenAlpha(0.0f);
    loadingScreenEffect_->SetEnabled(false);
}

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
    if (phase_ == TransitionPhase::Loading || phase_ == TransitionPhase::Changing) {
        loadingElapsed_ += deltaTime;
    }

    switch (phase_) {
    case TransitionPhase::FadeOut:
        // フェードアウト完了チェック
        if (timer_ >= duration_) {
            timer_ = duration_;
            // 完全暗転後、数フレーム待機してから次のフェーズへ移行
            waitFrameCounter_++;
            if (waitFrameCounter_ >= kWaitFramesAfterFadeOut) {
                if (type_ == TransitionType::Loading) {
                    phase_ = TransitionPhase::Loading;
                    timer_ = 0.0f;
                } else {
                    phase_ = TransitionPhase::Changing;
                }
            }
        }
        break;

    case TransitionPhase::Loading:
        // 最低表示時間を満たしたらシーン切り替えへ進む
        if (timer_ >= cvMinSeconds.Get()) {
            phase_ = TransitionPhase::Changing;
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

    // ローディング画面に表示強度を適用
    ApplyLoadingScreen();

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
    loadingElapsed_ = 0.0f;
    loadProgress_ = 0.0f;

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
    // フェードアウト中・ローディング中・Changing中はシーン更新をブロック
    return phase_ == TransitionPhase::FadeOut
        || phase_ == TransitionPhase::Loading
        || phase_ == TransitionPhase::Changing;
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
    ApplyLoadingScreen();
}

float SceneTransition::CalculateFadeAlpha() const {
    if (phase_ == TransitionPhase::Idle) {
        return 0.0f;
    }

    if (phase_ == TransitionPhase::Loading || phase_ == TransitionPhase::Changing) {
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

float SceneTransition::CalculateLoadingAlpha() const {
    if (type_ != TransitionType::Loading) {
        return 0.0f;
    }

    switch (phase_) {
    case TransitionPhase::Loading:
        // 暗転しきってから短くフェードインする
        return std::clamp(timer_ / kLoadingFadeSeconds, 0.0f, 1.0f);

    case TransitionPhase::Changing:
        return 1.0f;

    case TransitionPhase::FadeIn:
        // 背景が明るくなるより先に消す
        return 1.0f - std::clamp(timer_ / kLoadingFadeSeconds, 0.0f, 1.0f);

    default:
        return 0.0f;
    }
}

void SceneTransition::ApplyLoadingScreen() {
    if (!loadingScreenEffect_) {
        return;
    }

    float alpha = CalculateLoadingAlpha();
    loadingScreenEffect_->SetScreenAlpha(alpha);
    loadingScreenEffect_->SetProgress(loadProgress_);
    loadingScreenEffect_->SetGaugeAlpha(CalculateGaugeAlpha());
    loadingScreenEffect_->SetEnabled(alpha > 0.0f);
}

void SceneTransition::SetLoadProgress(float progress) {
    loadProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

float SceneTransition::CalculateGaugeAlpha() const {
    const float gaugeAfter = cvGaugeAfterSeconds.Get();
    if (type_ != TransitionType::Loading || gaugeAfter < 0.0f) {
        return 0.0f;
    }

    // 読み込みが長引いたときだけ現れる
    return std::clamp((loadingElapsed_ - gaugeAfter) / kGaugeFadeSeconds, 0.0f, 1.0f)
        * CalculateLoadingAlpha();
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

    case TransitionPhase::Loading:
    case TransitionPhase::Changing:
        // ローディング中・シーン切替中：完全に無音
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

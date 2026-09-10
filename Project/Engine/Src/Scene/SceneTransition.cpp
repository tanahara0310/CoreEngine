#include "pch.h"
#include "SceneTransition.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
#include "Graphics/PostEffect/Effect/ILoadingScreenEffect.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Audio/AudioSystem.h"
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

// ローディング画面エフェクトを取得（既定はエンジン汎用のもの。SetLoadingScreen で差し替えられる）
loadingScreenEffect_ = postEffectManager_->GetEffect<ILoadingScreenEffect>(PostEffectNames::LoadingScreen);

// トーンマッピングを取得（暗転中に自動露出の順応を止めるため）
toneMapping_ = postEffectManager_->GetEffect<ToneMapping>(PostEffectNames::ToneMapping);

// AudioSystem を取得（BGM バスのダッキングに使う）
audioSystem_ = engine_->GetService<AudioSystem>();

// 初期状態：完全に透明（フェードなし）
fadeEffect_->SetFadeAlpha(0.0f);
fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);
fadeEffect_->SetEnabled(false); // デフォルトは無効

if (loadingScreenEffect_) {
    loadingScreenEffect_->SetScreenAlpha(0.0f);
    loadingScreenEffect_->SetLoadingEnabled(false);
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
    if (phase_ == TransitionPhase::Loading || phase_ == TransitionPhase::Changing
        || phase_ == TransitionPhase::Hold) {
        loadingElapsed_ += deltaTime;
    }
    // 余韻は「到達してから」数える。loadingElapsed_ を足したあとに評価すること
    if (phase_ == TransitionPhase::Hold && CalculateDisplayProgress() >= 1.0f) {
        arrivedElapsed_ += deltaTime;
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
        // シーンの構築中。SceneManager が 1 フレーム 1 ステップずつ進め、
        // 終わったら OnSceneChanged() で Hold へ送ってくる。
        // ここで最低表示時間を待ってはいけない ―― 待ってから読み始めると、
        // その間ずっと進捗が 0 のまま止まって見える
        break;

    case TransitionPhase::Hold:
        // 構築は終わっている。表示進捗が 1.0 へ届き、その状態を見せる余韻を
        // 満たしたら進む。表示進捗の 1.0 到達には最低表示時間の条件も含まれている
        // （CalculateDisplayProgress が時間側と読み込み側の遅い方を取る）ので、
        // ここで loadingElapsed_ を重ねて見る必要は無い
        if (arrivedElapsed_ >= kHoldAfterArrivalSeconds) {
            phase_ = TransitionPhase::FadeIn;
            timer_ = 0.0f;
            waitFrameCounter_ = 0;
            fadeEffect_->SetEnabled(true);
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

    // 暗転しきっている間は自動露出を凍結する（フェードと同期）
    ApplyExposureHold();

    // BGM音量を適用（フェードと同期）
    ApplyBGMVolume();

    // 暗転中に始まった BGM は、画面が明けるまで頭で待たせる
    ApplyBGMStartHold();
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
    arrivedElapsed_ = 0.0f;

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

    // 次の Update を待たずに掛ける。ここから先で始まる BGM が保留の対象になる
    ApplyBGMStartHold();
}

bool SceneTransition::IsReadyToChangeScene() const {
    // Loading 系はローディング画面を出しながら構築する（Loading）。
    // それ以外は暗転しきった Changing で構築する。
    // Hold を含めてはいけない ―― 構築が終わった後もここが true だと、
    // SceneManager が BeginSceneLoad をもう一度呼んで読み直してしまう
    return phase_ == TransitionPhase::Loading || phase_ == TransitionPhase::Changing;
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

        // Update() は Idle だと即 return するので、ここで自分でダッキングと
        // 自動露出を戻す。忘れると Loading 中に 0 まで絞った BGM バスがそのまま
        // 無音で残り、露出も凍結したままになる
        ApplyExposureHold();
        ApplyBGMVolume();
    } else if (type_ == TransitionType::Loading) {
        // 構築は終わったが、最低表示時間はまだかもしれない。Hold で待ってから
        // フェードインする（進捗 1.0 の絵＝駅に着いた状態を必ず見せる）
        phase_ = TransitionPhase::Hold;
        arrivedElapsed_ = 0.0f;
        waitFrameCounter_ = 0;
    } else {
        // ローディング画面を出さない遷移はそのままフェードインへ
        phase_ = TransitionPhase::FadeIn;
        timer_ = 0.0f;
        waitFrameCounter_ = 0;
        fadeEffect_->SetEnabled(true);
    }

    // フェーズが変わった直後に反映する。ここで解除しないと、次の Update まで
    // BGM が頭で止まったままフェードインが進んでしまう
    ApplyBGMStartHold();
}

bool SceneTransition::IsTransitioning() const {
    return phase_ != TransitionPhase::Idle;
}

bool SceneTransition::IsBlocking() const {
    // フェードアウト中・ローディング中・Changing中・Hold中はシーン更新をブロック
    return phase_ == TransitionPhase::FadeOut
        || phase_ == TransitionPhase::Loading
        || phase_ == TransitionPhase::Changing
        || phase_ == TransitionPhase::Hold;
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

    // Update() は Idle だと即 return するので、ここで自分でダッキングと露出を戻す
    ApplyExposureHold();
    ApplyBGMVolume();
    ApplyBGMStartHold();
}

float SceneTransition::CalculateFadeAlpha() const {
    if (phase_ == TransitionPhase::Idle) {
        return 0.0f;
    }

    if (phase_ == TransitionPhase::Loading || phase_ == TransitionPhase::Changing
        || phase_ == TransitionPhase::Hold) {
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
    case TransitionPhase::Hold:
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
    loadingScreenEffect_->SetProgress(CalculateDisplayProgress());
    loadingScreenEffect_->SetGaugeAlpha(CalculateGaugeAlpha());
    loadingScreenEffect_->SetLoadingEnabled(alpha > 0.0f);
}

bool SceneTransition::SetLoadingScreen(const char* effectName) {
    if (!postEffectManager_ || !effectName) {
        return false;
    }

    auto* next = postEffectManager_->GetEffect<ILoadingScreenEffect>(effectName);
    if (!next || next == loadingScreenEffect_) {
        return next != nullptr;
    }

    // 旧画面を必ず消してから差し替える。チェーンには両方が並んでいるので、
    // 消し忘れると前の画面が有効なまま重なって描かれる
    if (loadingScreenEffect_) {
        loadingScreenEffect_->SetScreenAlpha(0.0f);
        loadingScreenEffect_->SetLoadingEnabled(false);
    }

    loadingScreenEffect_ = next;
    loadingScreenEffect_->SetScreenAlpha(0.0f);
    loadingScreenEffect_->SetLoadingEnabled(false);
    return true;
}

void SceneTransition::SetLoadProgress(float progress) {
    loadProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

float SceneTransition::CalculateDisplayProgress() const {
    if (type_ != TransitionType::Loading) {
        return loadProgress_;
    }

    const float minSeconds = cvMinSeconds.Get();
    const float timeRatio = (minSeconds > 0.0f)
        ? std::clamp(loadingElapsed_ / minSeconds, 0.0f, 1.0f)
        : 1.0f;

    // 遅い方に合わせる。読み込みが速ければ時間が、遅ければ読み込みが律速になる
    return std::min(loadProgress_, timeRatio);
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

void SceneTransition::ApplyExposureHold() {
    if (!toneMapping_) {
        return;
    }

    // 暗転しきっている間（Loading / Changing とフェードアウトの終わり際）は、
    // 旧シーンが解放されていて SceneColor が真っ黒。ここへ順応させると順応輝度が
    // 0 まで落ちて自動EVが上限へ張り付き、次のシーンが白飛びで現れる。
    // フェードインに入ってアルファが下がれば、そのまま新しいシーンへ順応が再開する。
    toneMapping_->SetAdaptationPaused(CalculateFadeAlpha() >= kExposureHoldAlpha);
}

void SceneTransition::ApplyBGMStartHold() {
    if (!audioSystem_) {
        return;
    }

    // 暗転している間（FadeOut / Loading / Changing / Hold）は、シーンが鳴らし始めた
    // BGM を頭で止めておく。フェードインへ入った時点で解除され、そこから鳴り出す
    audioSystem_->SetBusStartHold(AudioBus::BGM, IsBlocking());
}

void SceneTransition::ApplyBGMVolume() {
    if (!audioSystem_) {
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

    // BGM バスを丸ごと絞る。ダッキングはユーザー設定（SetBusVolume）とは
    // 別枠の倍率なので、オプション画面の設定値を壊さずに演出だけ掛かる
    audioSystem_->SetBusDuck(AudioBus::BGM, volumeMultiplier);
}
}

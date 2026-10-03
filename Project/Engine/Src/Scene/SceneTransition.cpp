#include "pch.h"
#include "SceneTransition.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/FadeEffect/FadeEffect.h"
#include "Graphics/PostEffect/Effect/ILoadingScreenEffect.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Audio/AudioSystem.h"
#include "Utility/CVar/CVar.h"

#include <algorithm>
#include <cassert>


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

    using TransitionPhase = SceneTransition::TransitionPhase;
    using Context = SceneTransition::Context;

    // フェードアウト完了後の待機フレーム数（完全暗転を確実にするため）
    constexpr int kWaitFramesAfterFadeOut = 3;

    // ローディング画面の表示強度が切り替わる時間（秒）
    constexpr float kLoadingFadeSeconds = 0.25f;

    // 進捗ゲージが現れるまでの時間（秒）
    constexpr float kGaugeFadeSeconds = 0.3f;

    // 表示進捗が 1.0 に届いてから必ず確保する余韻（秒）
    // 数え始めを「構築完了」にしてはいけない。読み込みが最低表示時間より
    // 速く終わると、構築完了の時点で余韻はとっくに満了しており、表示進捗が
    // 1.0 へ届いた瞬間にフェードインが始まる ―― 到達の絵が一度も見えない
    constexpr float kHoldAfterArrivalSeconds = 0.5f;

    // このフェードアルファ以上を「画面が見えていない」とみなす（自動露出の凍結境界）
    constexpr float kExposureHoldAlpha = 0.98f;

    /// @brief フェードの進み（0.0〜1.0）
    float FadeProgress(const Context& context)
    {
        return std::clamp(context.timer / context.duration, 0.0f, 1.0f);
    }

    // 種類ごとに通るフェーズの順番
    constexpr TransitionPhase kNoneRoute[] = { TransitionPhase::Changing };
    constexpr TransitionPhase kFadeRoute[] = {
        TransitionPhase::FadeOut, TransitionPhase::Changing, TransitionPhase::FadeIn };
    constexpr TransitionPhase kLoadingRoute[] = {
        TransitionPhase::FadeOut, TransitionPhase::Loading, TransitionPhase::Hold, TransitionPhase::FadeIn };

    SceneTransition::Route RouteOf(SceneTransition::TransitionType type)
    {
        switch (type) {
        case SceneTransition::TransitionType::Fade:
            return { kFadeRoute, true, false };
        case SceneTransition::TransitionType::Loading:
            return { kLoadingRoute, true, true };
        case SceneTransition::TransitionType::None:
        default:
            return { kNoneRoute, false, false };
        }
    }
}

/// @brief フェーズごとの答え（State パターンの状態）
/// @details 値は持たない。フェーズの間で受け渡す値は Context で受け取る。
///          答えは純粋仮想なので、フェーズを足すときに書き忘れるとコンパイルが通らない。
class SceneTransition::Phase {
public:
    virtual ~Phase() = default;

    /// @brief 入るときに今のフェーズの時間を 0 に戻す（ほかに戻す値があれば足す）
    virtual void OnEnter(Context& context) const { context.timer = 0.0f; }

    /// @brief 時間を進め、次のフェーズへ進むなら true
    virtual bool Update(Context& context, float deltaTime) const = 0;

    /// @brief シーンの組み立てが終わったら次のフェーズへ進むか
    virtual bool AdvancesOnSceneBuilt() const { return false; }

    /// @brief フェードの濃さ（0.0 = 透明, 1.0 = 不透明）
    virtual float FadeAlpha(const Context& context) const = 0;

    /// @brief ローディング画面の濃さ（ローディング画面を出す遷移のときだけ使われる）
    virtual float LoadingAlpha(const Context& context) const = 0;

    /// @brief BGM バスに掛ける倍率（1.0 = そのまま, 0.0 = 無音）
    virtual float BgmDuck(const Context& context) const = 0;

    /// @brief シーンの進行を止めるか（止めている間に始まった BGM は頭で待たせる）
    virtual bool BlocksScene() const = 0;

    /// @brief この間にシーンを組み立てるか
    virtual bool AcceptsSceneBuild() const { return false; }

    /// @brief ローディング画面を表示している時間に数えるか
    virtual bool CountsLoadingTime() const { return false; }
};

namespace
{
    class IdlePhase final : public SceneTransition::Phase {
    public:
        bool Update(Context&, float) const override { return false; }
        float FadeAlpha(const Context&) const override { return 0.0f; }
        float LoadingAlpha(const Context&) const override { return 0.0f; }
        float BgmDuck(const Context&) const override { return 1.0f; }
        bool BlocksScene() const override { return false; }
    };

    class FadeOutPhase final : public SceneTransition::Phase {
    public:
        void OnEnter(Context& context) const override
        {
            context.timer = 0.0f;
            context.waitFrameCounter = 0;
        }

        bool Update(Context& context, float) const override
        {
            if (context.timer < context.duration) {
                return false;
            }
            // 完全暗転後、数フレーム待機してから次のフェーズへ移行
            context.timer = context.duration;
            ++context.waitFrameCounter;
            return context.waitFrameCounter >= kWaitFramesAfterFadeOut;
        }

        // 0.0 → 1.0（徐々に暗くなる）
        float FadeAlpha(const Context& context) const override { return FadeProgress(context); }
        float LoadingAlpha(const Context&) const override { return 0.0f; }
        // 音量を徐々に下げる（1.0 → 0.0）
        float BgmDuck(const Context& context) const override { return 1.0f - FadeAlpha(context); }
        bool BlocksScene() const override { return true; }
    };

    class LoadingPhase final : public SceneTransition::Phase {
    public:
        // シーンの構築中。SceneManager が 1 フレーム 1 ステップずつ進め、
        // 終わったら OnSceneChanged() で次へ送ってくる。
        // ここで最低表示時間を待ってはいけない ―― 待ってから読み始めると、
        // その間ずっと進捗が 0 のまま止まって見える
        bool Update(Context&, float) const override { return false; }
        bool AdvancesOnSceneBuilt() const override { return true; }
        float FadeAlpha(const Context&) const override { return 1.0f; }
        // 暗転しきってから短くフェードインする
        float LoadingAlpha(const Context& context) const override
        {
            return std::clamp(context.timer / kLoadingFadeSeconds, 0.0f, 1.0f);
        }
        float BgmDuck(const Context&) const override { return 0.0f; }
        bool BlocksScene() const override { return true; }
        bool AcceptsSceneBuild() const override { return true; }
        bool CountsLoadingTime() const override { return true; }
    };

    class ChangingPhase final : public SceneTransition::Phase {
    public:
        // シーン切り替え待機中（完全に黒のまま維持）。組み立てが終わったら次へ
        bool Update(Context&, float) const override { return false; }
        bool AdvancesOnSceneBuilt() const override { return true; }
        float FadeAlpha(const Context&) const override { return 1.0f; }
        float LoadingAlpha(const Context&) const override { return 1.0f; }
        float BgmDuck(const Context&) const override { return 0.0f; }
        bool BlocksScene() const override { return true; }
        bool AcceptsSceneBuild() const override { return true; }
        bool CountsLoadingTime() const override { return true; }
    };

    class HoldPhase final : public SceneTransition::Phase {
    public:
        void OnEnter(Context& context) const override
        {
            context.timer = 0.0f;
            context.arrivedElapsed = 0.0f;
        }

        // 構築は終わっている。表示進捗が 1.0 へ届き、その状態を見せる余韻を
        // 満たしたら進む。表示進捗の 1.0 到達には最低表示時間の条件も含まれている
        // （DisplayProgress が時間側と読み込み側の遅い方を取る）ので、
        // ここで loadingElapsed を重ねて見る必要は無い
        bool Update(Context& context, float deltaTime) const override
        {
            // 余韻は「到達してから」数える。loadingElapsed を足したあとに評価する
            if (context.DisplayProgress() >= 1.0f) {
                context.arrivedElapsed += deltaTime;
            }
            return context.arrivedElapsed >= kHoldAfterArrivalSeconds;
        }

        float FadeAlpha(const Context&) const override { return 1.0f; }
        float LoadingAlpha(const Context&) const override { return 1.0f; }
        float BgmDuck(const Context&) const override { return 0.0f; }
        bool BlocksScene() const override { return true; }
        bool CountsLoadingTime() const override { return true; }
    };

    class FadeInPhase final : public SceneTransition::Phase {
    public:
        bool Update(Context& context, float) const override { return context.timer >= context.duration; }
        // 1.0 → 0.0（徐々に明るくなる）
        float FadeAlpha(const Context& context) const override { return 1.0f - FadeProgress(context); }
        // 背景が明るくなるより先に消す
        float LoadingAlpha(const Context& context) const override
        {
            return 1.0f - std::clamp(context.timer / kLoadingFadeSeconds, 0.0f, 1.0f);
        }
        // 音量を徐々に上げる（0.0 → 1.0）
        float BgmDuck(const Context& context) const override { return 1.0f - FadeAlpha(context); }
        bool BlocksScene() const override { return false; }
    };

    const SceneTransition::Phase& PhaseOf(TransitionPhase phase)
    {
        static const IdlePhase kIdle;
        static const FadeOutPhase kFadeOut;
        static const LoadingPhase kLoading;
        static const ChangingPhase kChanging;
        static const HoldPhase kHold;
        static const FadeInPhase kFadeIn;

        switch (phase) {
        case TransitionPhase::FadeOut:  return kFadeOut;
        case TransitionPhase::Loading:  return kLoading;
        case TransitionPhase::Changing: return kChanging;
        case TransitionPhase::Hold:     return kHold;
        case TransitionPhase::FadeIn:   return kFadeIn;
        case TransitionPhase::Idle:
        default:                        return kIdle;
        }
    }
}

float SceneTransition::Context::DisplayProgress() const {
    if (!showsLoadingScreen) {
        return loadProgress;
    }

    const float minSeconds = cvMinSeconds.Get();
    const float timeRatio = (minSeconds > 0.0f)
        ? std::clamp(loadingElapsed / minSeconds, 0.0f, 1.0f)
        : 1.0f;

    // 遅い方に合わせる。読み込みが速ければ時間が、遅ければ読み込みが律速になる
    return std::min(loadProgress, timeRatio);
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
    current_ = &PhaseOf(phase_);
    route_ = RouteOf(TransitionType::None);
    routeIndex_ = 0;
    context_ = Context{};
}

void SceneTransition::Update(float deltaTime) {
    if (!fadeEffect_ || phase_ == TransitionPhase::Idle) {
        return;
    }

    context_.timer += deltaTime;
    if (current_->CountsLoadingTime()) {
        context_.loadingElapsed += deltaTime;
    }

    // 進んだときは ChangePhase が反映する
    if (current_->Update(context_, deltaTime)) {
        AdvanceRoute();
        return;
    }
    Apply();
}

void SceneTransition::StartTransition(TransitionType type, float duration) {
    if (!fadeEffect_) {
        return;
    }

    route_ = RouteOf(type);
    assert(!route_.phases.empty());

    context_ = Context{};
    context_.duration = duration;
    context_.showsLoadingScreen = route_.showsLoadingScreen;

    fadeEffect_->SetFadeType(FadeEffect::FadeType::BlackFade);

    // 次の Update を待たずに反映する。ここから先で始まる BGM が保留の対象になる
    routeIndex_ = 0;
    ChangePhase(route_.phases[0]);
}

bool SceneTransition::IsReadyToChangeScene() const {
    // Loading 系はローディング画面を出しながら構築する（Loading）。
    // それ以外は暗転しきった Changing で構築する。
    // Hold を含めてはいけない ―― 構築が終わった後もここが true だと、
    // SceneManager が BeginSceneLoad をもう一度呼んで読み直してしまう
    return current_ && current_->AcceptsSceneBuild();
}

void SceneTransition::OnSceneChanged() {
    if (!fadeEffect_ || !current_->AdvancesOnSceneBuilt()) {
        return;
    }

    // Loading 系は Hold で最低表示時間を待ってからフェードインする（進捗 1.0 の絵を必ず見せる）。
    // ローディング画面を出さない遷移はそのままフェードインへ、遷移なしは待機へ戻る
    AdvanceRoute();
}

bool SceneTransition::IsBlocking() const {
    // フェードアウト中・ローディング中・Changing中・Hold中はシーン更新をブロック
    return current_ && current_->BlocksScene();
}

void SceneTransition::SkipTransition() {
    if (!fadeEffect_) {
        return;
    }
    ChangePhase(TransitionPhase::Idle);
}

void SceneTransition::ChangePhase(TransitionPhase next) {
    phase_ = next;
    current_ = &PhaseOf(next);
    current_->OnEnter(context_);

    // フェーズが変わった直後に反映する。ここで解除しないと、次の Update まで
    // BGM が頭で止まったままフェードインが進んでしまう
    Apply();
}

void SceneTransition::AdvanceRoute() {
    ++routeIndex_;
    ChangePhase(routeIndex_ < route_.phases.size() ? route_.phases[routeIndex_] : TransitionPhase::Idle);
}

void SceneTransition::Apply() {
    const float fadeAlpha = current_->FadeAlpha(context_);

    // フェードは遷移の間だけ有効にする（待機中は無効）
    fadeEffect_->SetEnabled(phase_ != TransitionPhase::Idle && route_.usesFade);
    fadeEffect_->SetFadeAlpha(fadeAlpha);

    if (loadingScreenEffect_) {
        const float loadingAlpha = context_.showsLoadingScreen ? current_->LoadingAlpha(context_) : 0.0f;
        loadingScreenEffect_->SetScreenAlpha(loadingAlpha);
        loadingScreenEffect_->SetProgress(context_.DisplayProgress());
        loadingScreenEffect_->SetGaugeAlpha(CalculateGaugeAlpha(loadingAlpha));
        loadingScreenEffect_->SetLoadingEnabled(loadingAlpha > 0.0f);
    }

    // 暗転しきっている間は、旧シーンが解放されていて SceneColor が真っ黒。ここへ順応させると
    // 順応輝度が 0 まで落ちて自動EVが上限へ張り付き、次のシーンが白飛びで現れる。
    // フェードインに入ってアルファが下がれば、そのまま新しいシーンへ順応が再開する。
    if (toneMapping_) {
        toneMapping_->SetAdaptationPaused(fadeAlpha >= kExposureHoldAlpha);
    }

    if (audioSystem_) {
        // BGM バスを丸ごと絞る。ダッキングはユーザー設定（SetBusVolume）とは
        // 別枠の倍率なので、オプション画面の設定値を壊さずに演出だけ掛かる
        audioSystem_->SetBusDuck(AudioBus::BGM, current_->BgmDuck(context_));

        // シーンを止めている間に始まった BGM は頭で止めておく。ダッキングは音量を 0 にするだけで
        // 曲は進むので、画面が明けた時に頭が聞こえない。保留は再生位置ごと止めるので、
        // フェードインの開始と同時に必ず曲の頭から鳴り出す
        audioSystem_->SetBusStartHold(AudioBus::BGM, current_->BlocksScene());
    }
}

float SceneTransition::CalculateGaugeAlpha(float loadingAlpha) const {
    const float gaugeAfter = cvGaugeAfterSeconds.Get();
    if (!context_.showsLoadingScreen || gaugeAfter < 0.0f) {
        return 0.0f;
    }

    // 読み込みが長引いたときだけ現れる
    return std::clamp((context_.loadingElapsed - gaugeAfter) / kGaugeFadeSeconds, 0.0f, 1.0f) * loadingAlpha;
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
    context_.loadProgress = std::clamp(progress, 0.0f, 1.0f);
}
}

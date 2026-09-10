#include "pch.h"
#include "PauseMenuFeature.h"

#include "Audio/AudioSystem.h"
#include "Components/GameCore/GameManagerComponent.h"
#include "Components/UI/PauseMenuUIComponent.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Scene/SceneManager.h"
#include "UI/UIImage.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <optional>

using namespace CoreEngine;

namespace
{
    // オーナー（入れ物）用。中身は PauseMenuUIComponent が組み立てる
    constexpr const char* kRootTexture = "Application/Assets/Textures/Pause/dim.png";

    constexpr const char* kSeOpen = "Application/Assets/Sounds/SE/title_bound.mp3";
    constexpr const char* kSeMove = "Application/Assets/Sounds/SE/build.mp3";
    constexpr const char* kSeConfirm = "Application/Assets/Sounds/SE/decision.mp3";
    constexpr const char* kSeClose = "Application/Assets/Sounds/SE/build_return.mp3";

    /// 決定してから実際に動くまでの待ち。札がへこんで跳ね返る演出を見せる時間
    constexpr float kConfirmDelaySeconds = 0.34f;

    CVar<bool> cvEnabled{
        "Game.PauseMenu.Enabled", true,
        "ポーズメニューを有効にする" };

    CVar<float> cvBgmDuck{
        "Game.PauseMenu.BgmDuck", 0.35f,
        "ポーズ中に BGM を絞る倍率（1.0 で絞らない）",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvSeVolume{
        "Game.PauseMenu.SeVolume", 0.6f,
        "ポーズメニューの効果音の音量",
        CVarRange{ 0.0f, 1.0f } };

    /// @brief 看板を吊るして開閉と入力を受け持つ Feature
    /// @details 停止中も回る（RunsWhileStopped）。止まっているあいだ
    ///          GameObject の Update() は呼ばれないので、メニューの動きは
    ///          ここから PauseMenuUIComponent::Tick() を叩いて進める。
    class PauseMenuFeature final : public ISceneFeature
    {
    public:
        const char* GetName() const override { return "PauseMenu"; }

        /// @details シーンの OnInitialize() が終わった後に呼ばれるフックなので、
        ///          この時点なら GameManagerComponent が既に生成されている。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }
            engine_ = ctx.engine;
            sceneManager_ = ctx.sceneManager;
            // 暗幕で画面を沈めても、自動露出が持ち上げ返して効かなくなる。
            // ポーズ中は順応を止める（シーン遷移の暗転と同じ扱い）
            if (auto* postEffects = engine_ ? engine_->GetService<PostEffectManager>() : nullptr) {
                toneMapping_ = postEffects->GetEffect<ToneMapping>(PostEffectNames::ToneMapping);
            }
            gameManager_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::GameManagerComponent>();

            auto* root = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>());
            if (!root) {
                return;
            }
            root->Initialize(kRootTexture, "PauseMenuRoot");
            root->SetSerializeEnabled(false);
            menu_ = root->AddComponent<GameComponents::PauseMenuUIComponent>();
            if (!menu_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "PauseMenuFeature: メニューを作れませんでした");
            }
        }

        void Update(SceneContext&, SceneUpdatePhase phase) override
        {
            if (phase != SceneUpdatePhase::FrameStart || !menu_) {
                return;
            }

            const float deltaTime = Time::UnscaledDeltaTime();

            // 自動露出はこのプロジェクトで有効（CVars.json）。UI もトーンマップ前の
            // バッファへ描かれるので、掛かる露出をここで打ち消す。でないと
            // 明るい場所と暗い場所でメニューの色が変わってしまう
            menu_->SetExposureScale(
                toneMapping_ ? std::exp2(-toneMapping_->GetAutoExposureEV()) : 1.0f);

            // 決定してからの待ち。ここは停止中も進める必要があるので実測時間で数える
            if (pendingChoice_.has_value()) {
                pendingTimer_ -= deltaTime;
                if (pendingTimer_ <= 0.0f) {
                    Commit(*pendingChoice_);
                }
            } else {
                HandleInput();
            }

            menu_->Tick(deltaTime);
        }

        /// @brief 停止中も回す。止めた本人が解除できないと詰む
        bool RunsWhileStopped() const override { return true; }

        /// @brief シーンを抜けるときに必ず再生へ戻す
        /// @details 止めたまま次のシーンへ行くと、そのシーンも動かないまま固まる
        void Finalize(SceneContext&) override
        {
            Resume();
        }

    private:
        void HandleInput()
        {
            auto* inputManager = engine_ ? engine_->GetService<InputManager>() : nullptr;
            if (!inputManager) {
                return;
            }
            const InputQuery& input = inputManager->GetQuery();

            // 文言の差し替えは頂点を組み直すので、変わったときだけ呼ぶ
            const bool gamepadConnected = input.IsGamepadConnected();
            if (gamepadConnected != gamepadHint_) {
                gamepadHint_ = gamepadConnected;
                menu_->SetHintForGamepad(gamepadConnected);
            }

            // ESC ／ パッドの START。開閉のトグル
            if (input.IsActionTriggered(InputAction::Pause)) {
                menu_->IsOpen() ? RequestClose() : RequestOpen();
                return;
            }
            if (!menu_->IsOpen()) {
                return;
            }

            // ESC ／ パッドの B。メニューの中では「戻る」として効かせる
            if (input.IsActionTriggered(InputAction::UICancel)) {
                RequestClose();
                return;
            }
            if (input.IsActionTriggered(InputAction::MoveForward)) {
                menu_->MoveSelection(-1);
                PlaySe(kSeMove, 1.3f);
            } else if (input.IsActionTriggered(InputAction::MoveBack)) {
                menu_->MoveSelection(1);
                PlaySe(kSeMove, 1.3f);
            }
            if (input.IsActionTriggered(InputAction::UIConfirm)) {
                menu_->PlayConfirm();
                PlaySe(kSeConfirm, 1.0f);
                pendingChoice_ = menu_->GetSelection();
                pendingTimer_ = kConfirmDelaySeconds;
            }
        }

        void RequestOpen()
        {
            // 終了演出やシーン遷移が走っている最中に開くと、遷移と二重になる
            if (!cvEnabled.Get() || menu_->IsVisible()) {
                return;
            }
            if (gameManager_ &&
                gameManager_->GetPhase() != GameComponents::GameManagerComponent::Phase::Playing) {
                return;
            }
            menu_->Open();
            PlaybackStateManager::GetInstance().Stop();
            SetBgmDuck(cvBgmDuck.Get());
            SetAdaptationPaused(true);
            PlaySe(kSeOpen, 1.0f);
        }

        void RequestClose()
        {
            menu_->Close();
            Resume();
            PlaySe(kSeClose, 1.0f);
        }

        /// @brief 決定した項目を実行する（演出を見せ終えてから呼ばれる）
        void Commit(GameComponents::PauseMenuUIComponent::Choice choice)
        {
            pendingChoice_.reset();
            menu_->Close();
            // どの行き先でも先に再生へ戻す。止めたままシーンを変えると次が動かない
            Resume();

            using Choice = GameComponents::PauseMenuUIComponent::Choice;
            if (choice == Choice::Continue || !sceneManager_) {
                return;
            }
            const char* next = (choice == Choice::Retry) ? "GameScene" : "TitleScene";
            if (sceneManager_->HasScene(next)) {
                sceneManager_->ChangeScene(next);
            }
        }

        /// @brief ゲームの進行と BGM を元へ戻す
        void Resume()
        {
            PlaybackStateManager::GetInstance().Play();
            SetBgmDuck(1.0f);
            SetAdaptationPaused(false);
        }

        /// @brief 自動露出の順応を止める / 再開する
        void SetAdaptationPaused(bool paused)
        {
            if (toneMapping_) {
                toneMapping_->SetAdaptationPaused(paused);
            }
        }

        void SetBgmDuck(float duck)
        {
            if (auto* audio = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
                audio->SetBusDuck(AudioBus::BGM, std::clamp(duck, 0.0f, 1.0f));
            }
        }

        void PlaySe(const char* path, float pitch)
        {
            auto* audio = engine_ ? engine_->GetService<AudioSystem>() : nullptr;
            if (!audio) {
                return;
            }
            audio->PlayOneShot(
                path,
                { .bus = AudioBus::SE, .volume = cvSeVolume.Get(), .pitch = pitch });
        }

        EngineSystem* engine_ = nullptr;
        SceneManager* sceneManager_ = nullptr;
        GameComponents::GameManagerComponent* gameManager_ = nullptr;
        GameComponents::PauseMenuUIComponent* menu_ = nullptr;
        ToneMapping* toneMapping_ = nullptr;

        std::optional<GameComponents::PauseMenuUIComponent::Choice> pendingChoice_;
        float pendingTimer_ = 0.0f;
        bool gamepadHint_ = false;  ///< 今ヒントに出している操作機器（キーボード = false）
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreatePauseMenuFeature()
{
    return std::make_unique<PauseMenuFeature>();
}

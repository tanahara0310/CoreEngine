#include "pch.h"
#include "TitleScene.h"

#include "Scenes/TitleScene/TitleSceneModelSetup.h"
#include "Scenes/TitleScene/TitleSceneUi.h"
#include "Audio/AudioSystem.h"
#include "Components/Title/TitleTextAnimationComponent.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Scene/SceneManager.h"
#include "UI/UIText.h"

using namespace CoreEngine;

namespace
{
    constexpr const char* kTitleBgmPath = "Sounds/BGM/Title_bgm.mp3";
    constexpr const char* kDecisionSePath = "Sounds/SE/decision.mp3";
}

void TitleScene::TitleScene::OnInitialize()
{
    SetSceneName("TitleScene");

    // タイトルシーンでもエンジン標準の地面を使う。
    //
    // BaseScene はシーン初期化後に GroundFeature::PostSceneInitialize() を呼び、
    // ここが有効なら DefaultGround を自動生成する。以前ここで false を渡して
    // いたため、タイトルシーンだけ地面が生成されず、床と同じ高さに描かれる
    // グリッドも確認しにくい状態になっていた。
    SetDefaultGroundEnabled(true);

    // title.json / _scene.json に依存せず、必要なオブジェクトをコードで構築する。
    // 生成処理の詳細は専用ファイルへ分離し、このクラスは「シーンを組み立てる順番」
    // だけを担当する。
    const std::function<std::function<void()>()> createIntroCompletionCallback = [this] {
        ++pendingIntroAnimations_;
        return [this] { OnTitleIntroAnimationComplete(); };
    };

    TitleSceneModel::Build(
        [this](const std::string& name) {
            return CreateObject(name);
        },
        createIntroCompletionCallback);

    // UI の見た目・アニメーション設定は TitleSceneUi に閉じ込める。
    // シーン本体は、現在の入力デバイスを初期表示へ渡すだけにする。
    if (engine_) {
        if (auto* inputManager = engine_->GetService<InputManager>()) {
            gamepadConnected_ = inputManager->GetQuery().IsGamepadConnected();
        }
    }

    const TitleSceneUi::Elements ui = TitleSceneUi::Build(
        [this](const std::string& text,
               float fontSize,
               UIAnchor anchor,
               const Vector2& position,
               const Vector4& color,
               const std::string& name) -> UIText* {
            auto* hint = CreateText(text, fontSize, anchor, position, color, name);
            return hint;
        },
        gamepadConnected_,
        createIntroCompletionCallback);

    startHint_ = ui.startHint;
    introAnimationRegistrationComplete_ = true;
    StartTitleBgmIfReady();
}

void TitleScene::TitleScene::OnUpdate()
{
    auto* inputManager = engine_ ? engine_->GetService<InputManager>() : nullptr;
    if (!inputManager || startRequested_) {
        return;
    }

    InputQuery& input = inputManager->GetQuery();

    // 接続状態が途中で変わった場合も、表示文言をその場で切り替える。
    // XInput コントローラの抜き差しをしても、実際に受け付ける入力と画面表示が
    // 食い違わないようにする。
    const bool gamepadConnected = input.IsGamepadConnected();
    if (gamepadConnected != gamepadConnected_) {
        gamepadConnected_ = gamepadConnected;
        TitleSceneUi::UpdateStartPrompt(startHint_, gamepadConnected_);
    }

    // 入力設定に登録されている UIConfirm を使う。現在の既定設定では、ゲームパッド
    // のAボタンとキーボードのSPACEキーが登録されているため、キーコンフィグを
    // 変更した場合もタイトル画面だけ判定が取り残されない。
    if (input.IsActionTriggered(InputAction::UIConfirm)) {
        StartGame();
        return;
    }
}

void TitleScene::TitleScene::StartGame()
{
    if (startRequested_ || !sceneManager_) {
        return;
    }

    startRequested_ = true;

    if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
        audioSystem->PlayOneShot(
            kDecisionSePath,
            { .bus = AudioBus::SE });
    }

    if (startHint_) {
        if (auto* animation =
            startHint_->GetComponent<GameComponents::TitleTextAnimationComponent>()) {
            animation->PlayStartReaction([this] {
                if (sceneManager_) {
                    sceneManager_->ChangeScene("GameScene");
                }
            });
            return;
        }
    }

    sceneManager_->ChangeScene("GameScene");
}

void TitleScene::TitleScene::OnTitleIntroAnimationComplete()
{
    if (pendingIntroAnimations_ > 0) {
        --pendingIntroAnimations_;
    }
    StartTitleBgmIfReady();
}

void TitleScene::TitleScene::StartTitleBgmIfReady()
{
    if (!introAnimationRegistrationComplete_
        || pendingIntroAnimations_ != 0
        || titleBgmStarted_)
    {
        return;
    }

    auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr;
    if (!audioSystem) {
        return;
    }

    titleBgm_ = audioSystem->PlayScoped(
        kTitleBgmPath,
        { .bus = AudioBus::BGM, .loop = true, .volume = 1.0f / 3.0f });
    titleBgmStarted_ = true;
}

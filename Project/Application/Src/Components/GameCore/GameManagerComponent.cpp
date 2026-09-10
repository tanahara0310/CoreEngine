#include "pch.h"
#include "GameManagerComponent.h"

#include "Components/GameCore/GameResultData.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/Rail/RailBuilderComponent.h"
#include "Components/Train/TrainMovementComponent.h"
#include "Camera/Rig/CameraRig.h"
#include "Scene/SceneManager.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace {
    // 終了時に列車へ寄るカメラリグ。構図は Presets/CameraRigs/ の同名 json が持つ。
    constexpr const char* kEndingCloseUpRigName = "Train_CloseUp";

    // 寄り切るまでの秒数。リグへの繋ぎ時間と、待機開始までの時間を兼ねる。
    constexpr float kEndingCloseUpSeconds = 1.5f;
}

json GameComponents::GameManagerComponent::OnSerialize() const {
    return { { "resultTransitionDelay", defaultChangeDelay_ } };
}

void GameComponents::GameManagerComponent::OnDeserialize(const json& j) {
    defaultChangeDelay_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "resultTransitionDelay", defaultChangeDelay_));
}

#ifdef USE_IMGUI
bool GameComponents::GameManagerComponent::DrawInspector() {
    bool changed = ImGui::DragFloat(
        "リザルト遷移待機時間", &defaultChangeDelay_, 0.05f, 0.0f, 10.0f);
    const char* phaseName = phase_ == Phase::Playing ? "Playing"
        : phase_ == Phase::Ending ? "Ending" : "Transitioning";
    ImGui::TextDisabled("現在フェーズ: %s", phaseName);
    return changed;
}
#endif

void GameComponents::GameManagerComponent::Start() {
    // 終了要求を Start() でリセットしない（オブジェクトの更新順に依存させない）。
    if (!sceneManager_ || !sceneManager_->HasScene("ResultScene")) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "GameManager: SceneManager または ResultScene が未設定です");
        SetEnabled(false);
    }
}

void GameComponents::GameManagerComponent::SetGameplayComponents(
    TrainMovementComponent* train,
    RailBuilderComponent* builder,
    HungerComponent* hunger) {
    train_ = train;
    builder_ = builder;
    hunger_ = hunger;
}

void GameComponents::GameManagerComponent::LateUpdate() {
    if (phase_ == Phase::Ending) {
        UpdateEnding();
    }
}

void GameComponents::GameManagerComponent::RequestGameOver(float changeDelayTime) {
    BeginEnding(false, changeDelayTime);
}

void GameComponents::GameManagerComponent::RequestGameClear(float changeDelayTime) {
    BeginEnding(true, changeDelayTime);
}

void GameComponents::GameManagerComponent::BeginEnding(bool isClear, float changeDelayTime) {
    // 最初の終了理由を保持し、重複通知でタイマーをリセットしない。
    if (phase_ != Phase::Playing) {
        return;
    }

    isGameClear_ = isClear;
    isGameOver_ = !isClear;
    phase_ = Phase::Ending;
    changeDelayTimer_ = changeDelayTime >= 0.0f
        ? changeDelayTime
        : defaultChangeDelay_;

    // GameScene のオブジェクトが破棄される前に、リザルト用の共有データへ確定する。
    GameResultData::SetHorizontalProgressBlocks(
        train_ ? train_->GetHorizontalProgressBlocks() : 0);
    GameResultData::SetMonkeyCount(hunger_ ? hunger_->GetMonkeyCount() : 1);

    if (train_) {
        // 移動コンポーネントを止める前に、ゲームオーバー時だけ
        // 列車に乗っているサルだけの発射演出を開始する。
        if (!isClear) {
            train_->PlayGameOverLaunch();
        }
        train_->SetEnabled(false);
    }
    if (builder_) {
        builder_->SetEnabled(false);
    }

    // ゲームクリア時だけ列車へ寄る。ゲームオーバー時は通常のゲームカメラを維持する。
    closeUpTimer_ = 0.0f;
    if (isClear) {
        // 構図は Presets/CameraRigs/Train_CloseUp.json 側が持つ。ここは名前で呼ぶだけ。
        CameraRigActivateOptions closeUpOptions;
        closeUpOptions.blendSeconds = kEndingCloseUpSeconds;
        closeUpOptions.useUnscaledTime = true;
        closeUpTimer_ = CameraRig::Activate(kEndingCloseUpRigName, closeUpOptions)
            ? kEndingCloseUpSeconds
            : 0.0f;
    }

    Logger::GetInstance().Infof(
        LogCategory::Game,
        "GameManager: {}。終了演出後 {:.2f} 秒でリザルトへ",
        isClear ? "ゲームクリア" : "ゲームオーバー", changeDelayTimer_);
}

void GameComponents::GameManagerComponent::UpdateEnding() {
    // ゲームプレイ側のポーズ・スローでも終了処理が止まらない時間を使う。
    const float deltaTime = std::max(0.0f, Time::UnscaledDeltaTime());

    // 寄り演出がある場合は、列車へ寄り切ってから待機を開始する。
    if (closeUpTimer_ > 0.0f) {
        closeUpTimer_ -= deltaTime;
        return;
    }
    changeDelayTimer_ -= deltaTime;
    if (changeDelayTimer_ <= 0.0f) {
        TransitionToResult();
    }
}

void GameComponents::GameManagerComponent::TransitionToResult() {
    if (!sceneManager_ || !sceneManager_->HasScene("ResultScene")) {
        return;
    }

    // SceneManager は次フレームで切り替える。要求後は二度と送らない。
    phase_ = Phase::Transitioning;
    sceneManager_->ChangeScene("ResultScene");
}

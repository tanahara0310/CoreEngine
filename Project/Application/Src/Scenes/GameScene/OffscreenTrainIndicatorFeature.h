#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief トロッコが画面外にいる間、画面の端にアイコンと「あと○m」を出す Feature を作る
    /// @details 登録は `AddFeature(GameComponents::CreateOffscreenTrainIndicatorFeature())` の
    ///          1 行でよい。シーンにいる TrainMovementComponent / GameManagerComponent と
    ///          ゲーム視点カメラを自分で探して繋ぐので、シーン側から渡す必要はない。
    /// @note 生成した UI はシリアライズ対象から外してあるため、シーンの JSON には残らない。
    ///       見た目の調整は CVar `Game.TrainOffscreen.*`（インスペクターの「画面外トロッコ案内」）で行う。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateOffscreenTrainIndicatorFeature();
}

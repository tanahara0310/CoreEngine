#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief スタミナのバナナゲージをシーンへ足す Feature を作る
    /// @details 登録は `AddFeature(GameComponents::CreateStaminaGaugeFeature())` の 1 行でよい。
    ///          シーンにいる HungerComponent を自分で探して繋ぐので、シーン側から
    ///          コンポーネントを渡す必要はない。
    /// @note 生成した UI はシリアライズ対象から外してあるため、シーンの JSON には残らない。
    ///       見た目の調整は CVar `Game.StaminaGauge.*`（インスペクターの「スタミナゲージ」）で行う。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateStaminaGaugeFeature();
}

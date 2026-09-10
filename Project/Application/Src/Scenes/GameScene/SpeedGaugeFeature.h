#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief トロッコの速度計（km/h のオドメーター）をシーンへ足す Feature を作る
    /// @details 登録は `AddFeature(GameComponents::CreateSpeedGaugeFeature())` の 1 行でよい。
    ///          シーンにいる TrainMovementComponent を自分で探して繋ぐので、シーン側から
    ///          コンポーネントを渡す必要はない。
    /// @note 生成した UI はシリアライズ対象から外してあるため、シーンの JSON には残らない。
    ///       見た目の調整は CVar `Game.SpeedGauge.*`（インスペクターの「速度計」）で行う。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateSpeedGaugeFeature();
}

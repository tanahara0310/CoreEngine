#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief ツタで吊るした木の看板のポーズメニューをシーンへ足す Feature を作る
    /// @details 登録は `AddFeature(GameComponents::CreatePauseMenuFeature())` の 1 行でよい。
    ///          シーンにいる GameManagerComponent を自分で探して繋ぐので、シーン側から
    ///          コンポーネントを渡す必要はない。
    ///
    ///          開くと `PlaybackStateManager::Stop()` がゲームの更新ごと止めるため、
    ///          ポーズ中にレールが敷けてしまう事故が起きない。そのぶんコンポーネントの
    ///          `Update()` も止まるので、メニューの動きはこの Feature が
    ///          `Time::UnscaledDeltaTime()` で進める（Tween も停止中は動かない）。
    ///
    /// @note 生成した UI はシリアライズ対象から外してあるため、シーンの JSON には残らない。
    ///       見た目の調整は CVar `Game.PauseMenu.*`（インスペクターの「ポーズメニュー」）で行う。
    std::unique_ptr<CoreEngine::ISceneFeature> CreatePauseMenuFeature();
}

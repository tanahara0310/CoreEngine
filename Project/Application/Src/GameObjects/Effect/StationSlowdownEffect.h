#pragma once

#include <memory>

namespace CoreEngine {
    class ISceneFeature;
}

namespace GameComponents
{   
    /// @brief 駅でトロッコの速度が落ちる瞬間に、「なぜ落ちたか」を見せる Feature を作る
    /// @note 値はすべて CVar `Game.StationSlowdown.*` が持つ。実行中にインスペクターの
    ///       「ゲーム設定」から調整でき、CVars.json へ自動保存される。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateStationSlowdownEffectFeature();
}

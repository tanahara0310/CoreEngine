#pragma once

#include "Scene/BaseScene.h"

namespace SampleGame
{
    /// @brief アイテム集めのサンプルシーン。
    /// @details 生成のみを行い、ゲーム処理は PlayerControllerComponent が持つ。
    class SampleGameScene : public CoreEngine::BaseScene {
    public:
        void OnInitialize() override;
    };
}

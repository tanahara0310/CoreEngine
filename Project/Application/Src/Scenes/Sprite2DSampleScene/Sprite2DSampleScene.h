#pragma once

#include "Scene/BaseScene.h"

namespace Sprite2DSample
{
    /// @brief スプライト（2D）のサンプルシーン。
    /// @details 落ちてくるアイテムを左右移動で受け止める。
    class Sprite2DSampleScene : public CoreEngine::BaseScene {
    public:
        void OnInitialize() override;
    };
}

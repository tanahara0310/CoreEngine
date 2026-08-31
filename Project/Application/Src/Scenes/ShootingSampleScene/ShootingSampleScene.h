#pragma once

#include "Scene/BaseScene.h"

namespace ShootingSample
{
    /// @brief 弾を撃って敵を倒すサンプルシーン。
    /// @details 生成のみを行い、ゲーム処理は各コンポーネントが持つ。
    class ShootingSampleScene : public CoreEngine::BaseScene {
    public:
        void OnInitialize() override;
    };
}

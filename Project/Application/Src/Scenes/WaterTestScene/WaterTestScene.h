#pragma once

// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

class WaterTestScene : public CoreEngine::BaseScene {
public:

    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 補助 RenderView 要求を構築する（鏡像カメラ反射廃止により現在は空）
    std::vector<CoreEngine::RenderViewRequest> BuildRenderViewRequests() override;

};

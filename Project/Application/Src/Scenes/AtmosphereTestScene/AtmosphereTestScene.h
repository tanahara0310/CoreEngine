#pragma once

// シーン
#include "Scene/BaseScene.h"

// エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "AtmosphereEditorFacade.h"

/// @brief 大気散乱（Sky Atmosphere）検証専用シーン
/// @details 太陽用 DirectionalLight 1灯と床のみの隔離環境で、
///          大気散乱システムの実装・調整・検証を行う。
class AtmosphereTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 更新処理
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;

private:
    AtmosphereEditorFacade editorFacade_{};
};

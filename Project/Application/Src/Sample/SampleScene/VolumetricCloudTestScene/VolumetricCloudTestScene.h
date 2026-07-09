#pragma once

// シーン
#include "Scene/BaseScene.h"

// エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "CloudEditorFacade.h"

/// @brief ボリューメトリック雲（Volumetric Cloud）検証専用シーン
/// @details 太陽用 DirectionalLight 1灯と床のみの隔離環境で、
///          ボリューメトリック雲システムの実装・調整・検証を行う。
///          AtmosphereTestScene と同じ役割分担を踏襲している。
class VolumetricCloudTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 更新処理
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

private:
    CloudEditorFacade editorFacade_{};
};

#pragma once

// シーン
#include "Scene/BaseScene.h"

// エンジンシステム
#include "EngineSystem/EngineSystem.h"

/// @brief ボリューメトリック雲（Volumetric Cloud）検証専用シーン
/// @details 太陽用 DirectionalLight 1灯と床のみの隔離環境で、
///          ボリューメトリック雲システムの実装・調整・検証を行う。
///          編集 UI はエンジン常駐の VolumetricCloudEditor（DebugSubsystem 所有）が
///          全シーン共通で Environment ツリーへ登録するため、シーン側には無い。
class VolumetricCloudTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 更新処理
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;
};

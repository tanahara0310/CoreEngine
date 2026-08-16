#pragma once

// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "CameraShowcase.h"
#include "WaterSceneController.h"

class WaterTestScene : public CoreEngine::BaseScene {
public:

    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief シーン固有の更新（カット巡回演出の進行）
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief シーン固有の解放（Feature 破棄より前に UI 登録を解除する）
    void OnFinalize() override;

    /// @brief 補助 RenderView 要求を構築する（鏡像カメラ反射廃止により現在は空）
    std::vector<CoreEngine::RenderViewRequest> BuildRenderViewRequests() override;

private:
    /// @brief 水面エディタ UI
    /// @details 水面本体・波シミュレーション・リソース結線は WaterRenderFeature が持つ。
    ///          シーンは Feature を登録し、UI を Hierarchy へ出すだけになる。
    WaterSceneController waterController_{};

    /// @brief 起動カメラの構図を巡回させる演出（ワンカット → 黒フェード → 次の構図）
    CameraShowcase cameraShowcase_{};
};

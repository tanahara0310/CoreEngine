#pragma once

// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "WaterSceneController.h"

#include <utility>

class WaterTestScene : public CoreEngine::BaseScene {
public:

    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief 補助 RenderView 要求を構築する（鏡像カメラ反射廃止により現在は空）
    std::vector<CoreEngine::RenderViewRequest> BuildRenderViewRequests() override;

    /// @brief 現在の DXR 水面屈折用波面データを返す
    const CoreEngine::WaterSurfaceData* GetWaterRefractionSurfaceData() const override;

    template<typename TObject, typename... TArgs>
    TObject* CreateWaterSceneObject(TArgs&&... args) {
        return CreateObject<TObject>(std::forward<TArgs>(args)...);
    }

private:
    WaterSceneController waterController_{};
};



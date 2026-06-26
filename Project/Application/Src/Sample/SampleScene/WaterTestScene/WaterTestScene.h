#pragma once

// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "WaterReflectionPass.h"
#include "../../../../../../Application/Src/Sample/SampleScene/WaterTestScene/WaterSceneController.h"

#include <utility>

class WaterTestScene : public CoreEngine::BaseScene {
public:

    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief 水面反射用の補助 RenderView 要求を構築する
    std::vector<CoreEngine::RenderViewRequest> BuildRenderViewRequests() override;

    /// @brief 現在の DXR 水面屈折用波面データを返す
    const CoreEngine::WaterSurfaceData* GetWaterRefractionSurfaceData() const override;

    /// @brief 解放
    void Finalize() override;

    template<typename TObject, typename... TArgs>
    TObject* CreateWaterSceneObject(TArgs&&... args) {
        return CreateObject<TObject>(std::forward<TArgs>(args)...);
    }

private:
    void SetupWaterReflectionView(CoreEngine::ICamera* mainCamera, float planeHeight);
    void RestoreWaterReflectionView(CoreEngine::ICamera* mainCamera);
    void ApplyWaterRenderViewResult(const CoreEngine::RenderViewResult& result);

    /// @brief 水面平面反射パス（Step 4）
    WaterReflectionPass reflectionPass_;

    WaterSceneController waterController_{};
};



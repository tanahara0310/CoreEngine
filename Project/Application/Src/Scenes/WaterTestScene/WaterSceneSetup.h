#pragma once

#include "GameObjects/Model/ModelObject.h"
#include "GameObjects/Water/WaterPlaneObject.h"

class WaterTestScene;

namespace CoreEngine {
    class EngineSystem;
}

/// @brief WaterTestScene で使用する主要オブジェクト群
/// @details scene setup 完了後に runtime controller へ受け渡す。
struct WaterSceneObjects {
    WaterPlaneObject* waterPlane = nullptr;
    ModelObject* groundObject = nullptr;
};

/// @brief WaterTestScene の初期オブジェクト生成と初期状態設定を担当する
/// @details RuntimeController から scene 構築責務を切り離すための setup クラス。
class WaterSceneSetup {
public:
    /// @brief WaterTestScene 用のオブジェクト生成と初期状態設定を行う
    /// @param scene 生成先のシーン
    /// @param engine エンジンシステム
    /// @return 生成した Water 用オブジェクト群
    WaterSceneObjects SetupScene(WaterTestScene& scene, CoreEngine::EngineSystem& engine);

private:
    /// @brief 水面オブジェクトを生成して基本状態を設定する
    WaterPlaneObject* CreateWaterPlane(WaterTestScene& scene);

    /// @brief 水中地形オブジェクトを生成して初期状態を設定する
    ModelObject* CreateGroundObject(WaterTestScene& scene);

    /// @brief 水面マテリアルの初期値を設定する
    void ConfigureWaterMaterial(WaterPlaneObject* waterPlane);

    /// @brief 水中地形モデルの初期状態を設定する
    void ConfigureGroundObject(ModelObject* groundObject);
};

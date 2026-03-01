#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief Terrainモデルオブジェクト
class TerrainObject : public CoreEngine::GameObject {
public:
    /// @brief 初期化処理
    /// @param engine エンジンシステムへのポインタ
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Terrain"; }

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }
};


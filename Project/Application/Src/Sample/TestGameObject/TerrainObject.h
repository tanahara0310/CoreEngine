#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"

/// @brief Terrainモデルオブジェクト
class TerrainObject : public CoreEngine::ModelGameObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Terrain"; }

protected:
    std::string GetModelPath() const override { return "SampleAssets/terrain/terrain.obj"; }
    std::string GetTexturePath() const override { return "SampleAssets/terrain/grass.png"; }
    void OnInitialize() override;
};


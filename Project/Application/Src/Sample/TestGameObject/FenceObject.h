#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"

/// @brief Fenceモデルオブジェクト


class FenceObject : public CoreEngine::ModelGameObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Fence"; }

protected:
    std::string GetModelPath() const override { return "fence.obj"; }
    std::string GetTexturePath() const override { return "fence.png"; }
};


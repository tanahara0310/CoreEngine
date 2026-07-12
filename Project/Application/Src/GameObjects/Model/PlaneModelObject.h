#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"

class Plane : public CoreEngine::ModelGameObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Plane"; }

protected:
    std::string GetModelPath()   const override { return "plane.obj"; }
    std::string GetTexturePath() const override { return "white1x1.png"; }
};




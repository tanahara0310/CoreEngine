#pragma once

#include "GameObject/Model/ModelGameObject.h"

class PlaneModelObject : public CoreEngine::ModelGameObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "PlaneModelObject"; }

protected:
    std::string GetModelPath()   const override { return "plane.obj"; }
    std::string GetTexturePath() const override { return "white1x1.png"; }
};




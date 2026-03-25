#pragma once

#include "ObjectCommon/Model/AnimatedModelObject.h"

/// @brief Walkモデルオブジェクト
class WalkModelObject : public CoreEngine::AnimatedModelObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "WalkModel"; }

protected:
    std::string GetModelPath() const override { return "walk.gltf"; }
    std::string GetAnimationName() const override { return "walkAnimation"; }
    std::string GetTexturePath() const override { return "white.png"; }
    void OnInitialize() override;

private:
};


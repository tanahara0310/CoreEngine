#pragma once

#include "ObjectCommon/Model/AnimatedModelObject.h"

/// @brief SneakWalkモデルオブジェクト
class SneakWalkModelObject : public CoreEngine::AnimatedModelObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "SneakWalkModel"; }

protected:
    std::string GetModelPath() const override { return "sneakWalk.gltf"; }
    std::string GetAnimationName() const override { return "sneakWalkAnimation"; }
    std::string GetTexturePath() const override { return "white1x1.png"; }
    void OnInitialize() override;

private:
};


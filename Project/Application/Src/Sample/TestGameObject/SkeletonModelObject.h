#pragma once

#include "ObjectCommon/Model/AnimatedModelObject.h"

/// @brief Skeletonモデルオブジェクト
class SkeletonModelObject : public CoreEngine::AnimatedModelObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "SkeletonModel"; }

protected:
    std::string GetModelPath() const override { return "simpleSkin.gltf"; }
    std::string GetAnimationName() const override { return "simpleSkinAnimation"; }
    std::string GetTexturePath() const override { return "uvChecker.png"; }
    void OnInitialize() override;

private:
};


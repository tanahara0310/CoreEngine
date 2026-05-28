#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/RingMeshGenerator.h"

/// @brief リングプリミティブオブジェクト
class RingObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param outerRadius 外周半径
    /// @param innerRadius 内周半径
    /// @param divisions   分割数
    RingObject(float outerRadius = 1.0f, float innerRadius = 0.2f, uint32_t divisions = 32,
        std::string texturePath = "")
        : outerRadius_(outerRadius), innerRadius_(innerRadius), divisions_(divisions)
        , texturePath_(std::move(texturePath)) {
    }

    const char* GetObjectName() const override { return "Ring"; }

protected:
    std::string GetTexturePath() const override { return texturePath_; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override {
        return std::make_unique<CoreEngine::RingMeshGenerator>(outerRadius_, innerRadius_, divisions_);
    }

private:
    float       outerRadius_;
    float       innerRadius_;
    uint32_t    divisions_;
    std::string texturePath_;
};

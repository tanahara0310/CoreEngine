#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"

/// @brief リングプリミティブオブジェクト
class RingObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param outerRadius 外周半径
    /// @param innerRadius 内周半径
    /// @param divisions   分割数
    RingObject(float outerRadius = 1.0f, float innerRadius = 0.2f, uint32_t divisions = 32,
        std::string texturePath = "");

    const char* GetObjectName() const override;

protected:
    std::string GetTexturePath() const override;

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

private:
    float       outerRadius_;
    float       innerRadius_;
    uint32_t    divisions_;
    std::string texturePath_;
};

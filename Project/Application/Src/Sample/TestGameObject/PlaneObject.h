#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"

/// @brief 平面プリミティブオブジェクト
class PlaneObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param width    X軸方向のサイズ
    /// @param depth    Z軸方向のサイズ
    /// @param subdivisionsX X方向の分割数
    /// @param subdivisionsZ Z方向の分割数
    PlaneObject(float width = 1.0f, float depth = 1.0f,
                uint32_t subdivisionsX = 1, uint32_t subdivisionsZ = 1)
        : width_(width), depth_(depth)
        , subdivisionsX_(subdivisionsX), subdivisionsZ_(subdivisionsZ) {}

    const char* GetObjectName() const override { return "Plane"; }

protected:
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override {
        return std::make_unique<CoreEngine::PlaneMeshGenerator>(
            width_, depth_, subdivisionsX_, subdivisionsZ_);
    }

private:
    float    width_;
    float    depth_;
    uint32_t subdivisionsX_;
    uint32_t subdivisionsZ_;
};

#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"

/// @brief 平面プリミティブオブジェクト
class PlaneObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param width    X軸方向のサイズ
    /// @param depth    Z軸方向のサイズ
    /// @param subdivisionsX X方向の分割数
    /// @param subdivisionsZ Z方向の分割数
    PlaneObject(float width = 1.0f, float depth = 1.0f,
                uint32_t subdivisionsX = 1, uint32_t subdivisionsZ = 1);

    const char* GetObjectName() const override;

protected:
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

private:
    float    width_;
    float    depth_;
    uint32_t subdivisionsX_;
    uint32_t subdivisionsZ_;
};

#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/SphereMeshGenerator.h"

/// @brief 球体プリミティブオブジェクト
class PrimitiveSphereObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param radius 球体の半径
    /// @param slices 経度方向の分割数
    /// @param stacks 緯度方向の分割数
    PrimitiveSphereObject(float radius = 0.5f, uint32_t slices = 32, uint32_t stacks = 16)
        : radius_(radius), slices_(slices), stacks_(stacks) {}

    const char* GetObjectName() const override { return "PrimitiveSphere"; }

protected:
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override {
        return std::make_unique<CoreEngine::SphereMeshGenerator>(radius_, slices_, stacks_);
    }

private:
    float    radius_;
    uint32_t slices_;
    uint32_t stacks_;
};

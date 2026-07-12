#include "pch.h"
#include "PlaneObject.h"

#include "Graphics/Primitive/PlaneMeshGenerator.h"

PlaneObject::PlaneObject(
    float width,
    float depth,
    uint32_t subdivisionsX,
    uint32_t subdivisionsZ)
    : width_(width)
    , depth_(depth)
    , subdivisionsX_(subdivisionsX)
    , subdivisionsZ_(subdivisionsZ) {
}

const char* PlaneObject::GetObjectName() const {
    return "Plane";
}

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> PlaneObject::CreateMeshGenerator() const {
    return std::make_unique<CoreEngine::PlaneMeshGenerator>(
        width_,
        depth_,
        subdivisionsX_,
        subdivisionsZ_);
}

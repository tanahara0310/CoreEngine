#include "pch.h"
#include "CubeObject.h"

#include "Graphics/Primitive/CubeMeshGenerator.h"

CubeObject::CubeObject(float size)
    : width_(size)
    , height_(size)
    , depth_(size) {
}

CubeObject::CubeObject(float width, float height, float depth)
    : width_(width)
    , height_(height)
    , depth_(depth) {
}

const char* CubeObject::GetObjectName() const {
    return "Cube";
}

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CubeObject::CreateMeshGenerator() const {
    return std::make_unique<CoreEngine::CubeMeshGenerator>(
        width_,
        height_,
        depth_);
}

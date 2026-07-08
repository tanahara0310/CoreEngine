#include "pch.h"
#include "RingObject.h"

#include "Graphics/Primitive/RingMeshGenerator.h"

#include <utility>

RingObject::RingObject(
    float outerRadius,
    float innerRadius,
    uint32_t divisions,
    std::string texturePath)
    : outerRadius_(outerRadius)
    , innerRadius_(innerRadius)
    , divisions_(divisions)
    , texturePath_(std::move(texturePath)) {
}

const char* RingObject::GetObjectName() const {
    return "Ring";
}

std::string RingObject::GetTexturePath() const {
    return texturePath_;
}

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> RingObject::CreateMeshGenerator() const {
    return std::make_unique<CoreEngine::RingMeshGenerator>(
        outerRadius_,
        innerRadius_,
        divisions_);
}

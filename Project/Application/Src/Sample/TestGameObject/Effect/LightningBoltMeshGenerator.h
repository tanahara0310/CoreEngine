#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

class LightningBoltMeshGenerator : public CoreEngine::IPrimitiveMeshGenerator {
public:
    struct Settings {
        uint32_t seed = 1337;
        uint32_t segmentCount = 14;
        uint32_t branchCount = 5;
        float boltLength = 1.0f;
        float trunkJitter = 0.12f;
        float branchLengthMin = 0.18f;
        float branchLengthMax = 0.38f;
        float baseWidth = 0.030f;
        float tipWidth = 0.010f;
    };

    explicit LightningBoltMeshGenerator(Settings settings = {});

    CoreEngine::ModelData Generate() const override;
    std::string GetCacheKey() const override;

private:
    Settings settings_;
};

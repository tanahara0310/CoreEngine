#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

namespace CoreEngine
{
    /// @brief リングメッシュを生成するジェネレーター
    class RingMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        /// @param outerRadius 外周半径
        /// @param innerRadius 内周半径
        /// @param divisions 分割数（頂点数）
        RingMeshGenerator(float outerRadius = 1.0f, float innerRadius = 0.2f,
            uint32_t divisions = 32);

        ModelData Generate() const override;
        std::string GetCacheKey() const override;

    private:
        float    outerRadius_;
        float    innerRadius_;
        uint32_t divisions_;
    };
}

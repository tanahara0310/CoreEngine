#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

namespace CoreEngine
{
    /// @brief UV球体メッシュを生成するジェネレーター
    class SphereMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        /// @param radius 球体の半径
        /// @param slices 経度方向の分割数
        /// @param stacks 緯度方向の分割数
        SphereMeshGenerator(float radius = 0.5f,
                            uint32_t slices = 32, uint32_t stacks = 16);

        ModelData Generate() const override;
        std::string GetCacheKey() const override;

    private:
        float radius_;
        uint32_t slices_;
        uint32_t stacks_;
    };
}
#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

namespace CoreEngine
{
    /// @brief 平面メッシュを生成するジェネレーター
    class PlaneMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        /// @param width X軸方向のサイズ
        /// @param depth Z軸方向のサイズ
        /// @param subdivisionsX X方向の分割数
        /// @param subdivisionsZ Z方向の分割数
        PlaneMeshGenerator(float width = 1.0f, float depth = 1.0f,
                           uint32_t subdivisionsX = 1, uint32_t subdivisionsZ = 1);

        ModelData Generate() const override;
        std::string GetCacheKey() const override;

    private:
        float width_;
        float depth_;
        uint32_t subdivisionsX_;
        uint32_t subdivisionsZ_;
    };
}
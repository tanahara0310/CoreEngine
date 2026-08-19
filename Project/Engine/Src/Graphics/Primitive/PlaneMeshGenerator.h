#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

namespace CoreEngine
{
    /// @brief 平面メッシュを生成するジェネレーター
    class PlaneMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        /// @param uvTiling UV の繰り返し数（1.0 で端から端まで 0..1）
        /// @note uvTiling はサンプラーが WRAP 前提。板をスケールで巨大化するときに
        ///       テクスチャを引き伸ばさずタイル状に敷くために使う。
        PlaneMeshGenerator(float width = 1.0f, float depth = 1.0f,
                           uint32_t subdivisionsX = 1, uint32_t subdivisionsZ = 1,
                           float uvTiling = 1.0f);

        ModelData Generate() const override;
        std::string GetCacheKey() const override;

    private:
        float width_;
        float depth_;
        uint32_t subdivisionsX_;
        uint32_t subdivisionsZ_;
        float uvTiling_;
    };
}
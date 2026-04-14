#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

namespace CoreEngine
{
    /// @brief 立方体メッシュを生成するジェネレーター
    class CubeMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        /// @param size 各軸のサイズ（幅・高さ・奥行きが均等）
        explicit CubeMeshGenerator(float size = 1.0f);

        /// @param width X軸方向のサイズ
        /// @param height Y軸方向のサイズ
        /// @param depth Z軸方向のサイズ
        CubeMeshGenerator(float width, float height, float depth);

        ModelData Generate() const override;
        std::string GetCacheKey() const override;

    private:
        float width_;
        float height_;
        float depth_;
    };
}
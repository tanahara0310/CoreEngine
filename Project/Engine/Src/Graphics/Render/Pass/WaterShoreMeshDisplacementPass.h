#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 粗い格子と岸の細かい格子の頂点の変位を書くパス（FFT の波面を計算した後、水面の描画・RT の水面パス・高さの問い合わせより前）
    class WaterShoreMeshDisplacementPass final : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterShoreMeshDisplacementPass"; }
        void Execute(const RenderContext& context) override;
    };
}

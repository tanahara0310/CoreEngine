#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 物がかき回した所の泡と、物が起こした水の流れを進めるパス（水面を描くパスより前）
    class WaterObjectFoamPass final : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterObjectFoamPass"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}

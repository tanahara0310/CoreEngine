#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    class RTWaterCausticsPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "RTWaterCausticsPass"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}

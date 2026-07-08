#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    class WaterCausticsPass : public RenderPass
    {
    public:
        WaterCausticsPass() = default;
        ~WaterCausticsPass() override = default;

        const char* GetName() const override { return "WaterCausticsPass"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;

        void SetTargetName(const std::string& name) { targetName_ = name; }

    private:
        std::string targetName_ = RenderTargetNames::WaterCausticsBuffer;
    };
}

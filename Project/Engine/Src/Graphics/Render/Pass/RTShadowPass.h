#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief GameView 向け RT シャドウディスパッチを行うパス
    /// @details Step 2 段階では既存 RayTracingSubsystem::DispatchRTShadow() をラップし、
    ///          GBufferPass / SSAOPass の後、DeferredLightingPass の前に差し込む。
    class RTShadowPass : public RenderPass {
    public:
        RTShadowPass() = default;
        ~RTShadowPass() override = default;

        const char* GetName() const override { return "RTShadow"; }

        void Execute(const RenderContext& context) override;
    };
}

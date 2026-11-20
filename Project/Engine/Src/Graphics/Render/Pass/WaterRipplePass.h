#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 水の波紋の源を入れて波を進めるパス（水面と RT の水面パスより前）
    class WaterRipplePass final : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterRipplePass"; }
        void Execute(const RenderContext& context) override;
    };
}

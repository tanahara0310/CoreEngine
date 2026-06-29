#pragma once

#include "RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include <cstddef>
#include <optional>

namespace CoreEngine
{
    class GameObject;

    enum class RenderItemKind {
        Default,
        SkyBox,
        Transparent,
        WaterSurface,
    };

    struct RenderItem {
        GameObject* object = nullptr;
        RenderItemKind kind = RenderItemKind::Default;
        RenderPassType passType = RenderPassType::Invalid;
        BlendMode blendMode = BlendMode::kBlendModeNone;
        int sortKey = 0;
        std::optional<int> renderOrderOverride;
        size_t registrationOrder = 0;
    };
}

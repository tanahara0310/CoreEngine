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

    /// @brief 描画キューへ積む 1 件（対象オブジェクトと、どのパス・ブレンド・順序で描くか）
    struct RenderItem {
        GameObject* object = nullptr;
        RenderItemKind kind = RenderItemKind::Default;
        RenderPassType passType = RenderPassType::Invalid;
        BlendMode blendMode = BlendMode::kBlendModeNone;
        int sortingLayer = 0;
        int orderInLayer = 0;
        int sortKey = 0;
        float viewDepth = 0.0f;  ///< 半透明キューの並べ替えに使う、カメラの前方への距離
        std::optional<int> renderOrderOverride;
        size_t registrationOrder = 0;
    };
}

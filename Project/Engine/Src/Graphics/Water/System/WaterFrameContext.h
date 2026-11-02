#pragma once

namespace CoreEngine
{
    struct SceneContext;
    struct WaterSettings;
    class RenderDomainContext;
    class WaterSurfaceComponent;

    /// @brief 水の各モジュールへ 1 フレームの処理で渡す入力
    struct WaterFrameContext {
        SceneContext& scene;
        RenderDomainContext& domain;
        WaterSurfaceComponent& waterPlane;
        const WaterSettings& settings;
    };
}

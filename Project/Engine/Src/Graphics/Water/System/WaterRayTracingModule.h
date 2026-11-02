#pragma once

namespace CoreEngine
{
    class RenderDomainContext;
    struct WaterFrameBinding;
    struct WaterFrameContext;

    /// @brief レイトレーシングの屈折と反射を水面へつなぐ
    class WaterRayTracingModule {
    public:
        /// @brief 屈折の設定（屈折率・再投影のずれの上限）を渡す
        void ApplySettings(const WaterFrameContext& frame) const;

        /// @brief 屈折・反射・水面の日向率の SRV を結線へ書く
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;
    };
}

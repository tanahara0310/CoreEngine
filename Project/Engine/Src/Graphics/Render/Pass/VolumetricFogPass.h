#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief ボリュメトリックフォグの格子へ霧の濃さを書くパス（格子の用意もここで行う）
    /// @note GameView のみ。r.Fog.Volumetric.Enabled が立っているフレームだけ動く
    class VolumetricFogInjectPass : public RenderPass {
    public:
        const char* GetName() const override { return "VolumetricFogInject"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }
        void Execute(const RenderContext& context) override;
    };

    /// @brief ボリュメトリックフォグの格子へ、視線の向きへ散乱する光を書くパス
    class VolumetricFogLightScatteringPass : public RenderPass {
    public:
        const char* GetName() const override { return "VolumetricFogLightScattering"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }
        void Execute(const RenderContext& context) override;
    };

    /// @brief ボリュメトリックフォグの格子を手前から奥へ積み上げ、FogPass へ渡すパス
    class VolumetricFogIntegratePass : public RenderPass {
    public:
        const char* GetName() const override { return "VolumetricFogIntegrate"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }
        void Execute(const RenderContext& context) override;
    };
}

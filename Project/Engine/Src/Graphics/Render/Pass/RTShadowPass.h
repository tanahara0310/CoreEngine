#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief RT シャドウの DispatchRays ステージ
    /// @details GBuffer を入力にライトごとのレイトレーシングを実行する。
    ///          テンポラル蓄積・デノイズは後続の RTShadowTemporalPass / RTShadowDenoisePass が担う。
    class RTShadowPass : public RenderPass {
    public:
        RTShadowPass() = default;
        ~RTShadowPass() override = default;

        const char* GetName() const override { return "RTShadowTrace"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableRTShadow; }

        void Execute(const RenderContext& context) override;
    };

    /// @brief RT シャドウのテンポラル蓄積ステージ（空間前処理 + 再投影 + Variance Clamping）
    class RTShadowTemporalPass : public RenderPass {
    public:
        RTShadowTemporalPass() = default;
        ~RTShadowTemporalPass() override = default;

        const char* GetName() const override { return "RTShadowTemporal"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableRTShadow; }

        void Execute(const RenderContext& context) override;
    };

    /// @brief RT シャドウの A-Trous デノイズステージ
    class RTShadowDenoisePass : public RenderPass {
    public:
        RTShadowDenoisePass() = default;
        ~RTShadowDenoisePass() override = default;

        const char* GetName() const override { return "RTShadowDenoise"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableRTShadow; }

        void Execute(const RenderContext& context) override;
    };
}

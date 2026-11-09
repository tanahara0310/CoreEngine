#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief RTAO のレイを撃つ段
    /// @details 後の 2 段（RTAOTemporalPass・RTAODenoisePass）が時間の蓄積と A-Trous・仕上げを受け持つ。
    ///          どの段も IsRayTracedAOSelected が false のフレームは何もしない。
    class RTAOTracePass : public RenderPass {
    public:
        const char* GetName() const override { return "RTAOTrace"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableSSAO; }
        void Execute(const RenderContext& context) override;
    };

    /// @brief RTAO の時間方向の蓄積の段
    class RTAOTemporalPass : public RenderPass {
    public:
        const char* GetName() const override { return "RTAOTemporal"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableSSAO; }
        void Execute(const RenderContext& context) override;
    };

    /// @brief RTAO の A-Trous とフル解像度への仕上げの段（出力を FrameBlackboard::RTAO へ載せる）
    class RTAODenoisePass : public RenderPass {
    public:
        const char* GetName() const override { return "RTAODenoise"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableSSAO; }
        void Execute(const RenderContext& context) override;
    };

    /// @brief このフレームの環境光の遮りを SSAO の代わりに RTAO で作るか
    /// @details ビューが AO を使い、SSAO の切り替え（r.SSAO.Enabled）と r.RTAO.Enabled が ON で、
    ///          RTAO が使える状態（DXR 対応・デバッグ窓で止めていない）のとき true。
    bool IsRayTracedAOSelected(const RenderContext& context);
}

#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief ゴッドレイ（雲の隙間の光芒）パス（Compute）
    /// @details 雲シャドウマップを生成し、ビューレイに沿った内散乱の遮蔽差分を
    ///          半解像度でレイマーチして SceneColor へ合成する。
    ///          Sky フェーズの VolumetricCloudPass（20）の後（30）に実行される。
    ///          GameView のみで有効（ReflectionView は対象外）。
    ///          設計書: Docs/Engine/Graphics/Rendering/GodRay_Design.md
    class GodRayPass : public RenderPass {
    public:
        GodRayPass() = default;
        ~GodRayPass() override = default;

        /// @brief パス名を取得
        const char* GetName() const override { return "GodRay"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;
    };
}

#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief ゴッドレイ（雲の隙間の光芒）パス（Compute）
    /// @details 雲シャドウマップを作り、内散乱の遮蔽差分を半解像度でレイマーチして SceneColor へ合成する。
    /// @note Sky フェーズの VolumetricCloudPass の後に実行。GameView のみで有効。
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

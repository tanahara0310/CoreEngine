#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief ボリューメトリック雲のレイマーチ + 合成パス（Compute）
    /// @details SceneDepth と大気 LUT を参照して雲を半解像度でレイマーチし、
    ///          SkyBox 描画後の SceneColor へ「雲色 + シーン色 × 透過率」で合成する。
    ///          Sky フェーズ（SkyBoxQueuePass の後・Transparent の前）に実行される。
    ///          GameView のみで有効（ReflectionView は対象外）。
    class VolumetricCloudPass : public RenderPass {
    public:
        VolumetricCloudPass() = default;
        ~VolumetricCloudPass() override = default;

        /// @brief パス名を取得
        const char* GetName() const override { return "VolumetricCloud"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;
    };
}

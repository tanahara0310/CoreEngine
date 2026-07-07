#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 空気遠近感（Aerial Perspective）合成パス
    /// @details SceneDepth から求めた距離で Camera Volume LUT をサンプリングし、
    ///          SceneColor へ「シーン色 × 透過率 + inscattering」を合成する。
    ///          DeferredLightingPass の後・GeometryPass（SkyBox 描画）の前に実行される。
    ///          GameView のみで有効（ReflectionView は対象外）。
    class AerialPerspectivePass : public RenderPass {
    public:
        AerialPerspectivePass() = default;
        ~AerialPerspectivePass() override = default;

        /// @brief パス名を取得
        const char* GetName() const override { return "AerialPerspective"; }

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;
    };
}

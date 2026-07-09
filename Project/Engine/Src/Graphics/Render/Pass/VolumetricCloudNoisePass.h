#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief ボリューメトリック雲のノイズ生成パス（Compute）
    /// @details VolumetricCloudManager のダーティフラグが立っている場合のみ
    ///          Base Shape / Detail / Weather の各ノイズテクスチャを再生成する。
    ///          FrameSetup フェーズ（AtmosphereLUTPass の後）に実行される。
    ///          雲を使うシーン（Update() を呼ぶシーン）でのみ動作する。
    class VolumetricCloudNoisePass : public RenderPass {
    public:
        VolumetricCloudNoisePass() = default;
        ~VolumetricCloudNoisePass() override = default;

        /// @brief パス名を取得
        const char* GetName() const override { return "VolumetricCloudNoise"; }

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;
    };
}

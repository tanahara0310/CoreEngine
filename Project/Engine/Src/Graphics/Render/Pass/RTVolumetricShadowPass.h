#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief ボリュメトリックフォグの物の影に使う、太陽から見た深さの画像を撃つパス
    /// @details IsVolumetricShadowSelected が false のフレームは何もしない。
    ///          書いた画像は FrameBlackboard::VolumetricSunDepth へ載せ、霧の格子のパスが読む。
    class RTVolumetricShadowPass : public RenderPass {
    public:
        const char* GetName() const override { return "RTVolumetricShadow"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };

    /// @brief このフレームに太陽から見た深さの画像を撃つか
    /// @details ゲームの画面でボリュメトリックフォグを作り、効果が使える状態（DXR 対応・デバッグ窓で止めていない）のとき true
    bool IsVolumetricShadowSelected(const RenderContext& context);
}

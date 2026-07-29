#pragma once
#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief TAA（テンポラルアンチエイリアス）解決パス
    /// @details 水面合成まで終わった SceneColor を入力に、モーションベクターで再投影した
    ///          前フレームの結果と蓄積する。出力はそのまま次フレームの履歴になる。
    ///
    ///          配置は PostProcess フェーズの先頭（ポストエフェクト列より前）。
    ///          トーンマップ前の HDR 空間で解決するのが TAA の正しい位置で、
    ///          Bloom / LensFlare もアンチエイリアス済みの画を入力にできる。
    ///
    ///          出力先は履歴 ping-pong の片側なので SceneColor へは書き戻さない。
    ///          後続（ポストエフェクト列 / BackBuffer）の入力名は
    ///          RenderPipeline が TAAOutput へ差し替える。
    class TAAPass : public RenderPass {
    public:
        TAAPass() = default;
        ~TAAPass() override = default;

        const char* GetName() const override { return "TAAPass"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief 補助ビューでは実行しない
        /// @details 履歴もモーションベクターも GameView 専用（Model の prevWVP が GameView 基準）。
        ///          反射ビューで走らせると履歴が別カメラの画で汚染される。
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void Execute(const RenderContext& context) override;
    };
}

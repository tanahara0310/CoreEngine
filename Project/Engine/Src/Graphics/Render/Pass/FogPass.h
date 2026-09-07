#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 高さフォグ合成パス
    /// @details SceneDepth からワールド座標を復元し、SceneColor へ
    ///          「シーン色 と フォグ色 を透過率で混ぜた結果」を in-place 合成する。
    ///          SkyBox / 雲 / ゴッドレイの後・半透明の前に実行される（背景ピクセルも
    ///          フォグの対象にするため、空が描かれた後である必要がある）。
    ///          GameView のみで有効（ReflectionView は対象外）。
    /// @note 半透明・水面は本パスの後に描かれるためここでは掛からない。
    ///       それらは各シェーダーで Fog.hlsli の ApplyFog を呼ぶこと（Phase 3）。
    class FogPass : public RenderPass {
    public:
        FogPass() = default;
        ~FogPass() override = default;

        /// @brief パス名を取得
        const char* GetName() const override { return "Fog"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @note どの View でも登録する。SceneColor へ合成するのは GameView だけだが、
        ///       前方描画が読むフォグ定数はどの View でも用意する必要がある

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;
    };
}

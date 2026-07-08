#pragma once
#include "RenderPass.h"
#include <string>

namespace CoreEngine
{
    class RenderTarget;

    /// @brief Forward 系キューを SceneColor へ重ね描きするパスの共通基底
    /// @details SceneColor 読み書きと SceneDepth 参照の宣言、View 依存の出力先切り替え、
    ///          レンダーターゲットの Begin/End とキュー描画の骨格を共通化する。
    ///          派生クラスは対象キューの描画のみを実装する。
    class ForwardQueuePassBase : public RenderPass {
    public:
        ~ForwardQueuePassBase() override = default;

        /// @brief SceneColor に上乗せしつつ SceneDepth を read-only DSV/SRV として参照する
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief View の SceneColor ターゲット名を出力先へ反映する
        void ConfigureForView(const RenderContext& context) override;

        void Execute(const RenderContext& context) override;

        /// @brief レンダーターゲット名を設定
        void SetRenderTargetName(const std::string& name) { targetName_ = name; }

        /// @brief 設定されているターゲット名を取得
        const std::string& GetRenderTargetName() const { return targetName_; }

    protected:
        /// @brief 担当キューを描画する（RenderTarget の Begin/End 内で呼ばれる）
        /// @param context レンダリングコンテキスト
        virtual void DrawQueue(const RenderContext& context) = 0;

        /// @brief Execute() 開始時にレンダーターゲットをクリアするか
        virtual bool ShouldClear() const { return false; }

        std::string targetName_ = RenderTargetNames::SceneColor;
    };

    /// @brief Forward メインキュー（パーティクル / ライン / スプライト / 透過ブレンドモデル等）の描画パス
    /// @details 不透明 Model/SkinnedModel は投入時に GBuffer 経路へ振り分け済みのため含まれない。
    class GeometryPass : public ForwardQueuePassBase {
    public:
        GeometryPass() = default;
        ~GeometryPass() override = default;

        const char* GetName() const override { return "Geometry"; }

        /// @brief Execute() 開始時にレンダーターゲットをクリアするか設定する
        /// @note DeferredLightingPass が前段に書き込み済みの場合は false を指定する
        void SetClearEnabled(bool enabled) { clearEnabled_ = enabled; }

        /// @brief クリアが有効かどうか取得
        bool IsClearEnabled() const { return clearEnabled_; }

    protected:
        void DrawQueue(const RenderContext& context) override;
        bool ShouldClear() const override { return clearEnabled_; }

    private:
        bool clearEnabled_ = true;
    };

    /// @brief SkyBox キューの描画パス
    class SkyBoxQueuePass : public ForwardQueuePassBase {
    public:
        SkyBoxQueuePass() = default;
        ~SkyBoxQueuePass() override = default;

        const char* GetName() const override { return "SkyBoxQueue"; }

    protected:
        void DrawQueue(const RenderContext& context) override;
    };

    /// @brief 透明オブジェクトキューの描画パス
    class TransparentQueuePass : public ForwardQueuePassBase {
    public:
        TransparentQueuePass() = default;
        ~TransparentQueuePass() override = default;

        const char* GetName() const override { return "TransparentQueue"; }

    protected:
        void DrawQueue(const RenderContext& context) override;
    };
}

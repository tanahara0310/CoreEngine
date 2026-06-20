#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"

namespace CoreEngine{

    /// @brief GBuffer の内容を元にライティングを行う描画パス
    /// Deferred Rendering のライティングパスに相当。GBufferPass の後に実行される。
    class DeferredLightingPass : public RenderPass {
    public:
        DeferredLightingPass() = default;
        ~DeferredLightingPass() override = default;

        /// @brief パス名を取得
        /// @return パス名
        const char* GetName() const override { return "DeferredLighting"; }

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;

        /// @brief パスのセットアップ（リソース準備など）
        /// @param context レンダリングコンテキスト
        void Setup(const RenderContext& context) override;

        /// @brief 出力先レンダーターゲット名を設定
        /// @param name レンダーターゲット名（RenderTargetManager に登録されている名前を指定）
        void SetRenderTargetName(const std::string& name) { targetName_ = name; }

    private:
        std::string targetName_ = RenderTargetNames::SceneColor;
    };
}

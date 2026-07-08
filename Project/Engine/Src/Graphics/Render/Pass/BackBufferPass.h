#pragma once
#include "RenderPass.h"
#include <d3d12.h>
#include <string>

namespace CoreEngine
{
    class RenderTarget;

    /// @brief バックバッファへの最終出力パス
    class BackBufferPass : public RenderPass {
    public:
        BackBufferPass() = default;
        ~BackBufferPass() override = default;

        const char* GetName() const override { return "BackBuffer"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enableBackBuffer; }

        void Execute(const RenderContext& context) override;

        /// @brief レンダーターゲット名を設定
        /// @param name ターゲット名
        void SetRenderTargetName(const std::string& name) {
            targetName_ = name;
        }

        /// @brief 設定されているターゲット名を取得
        /// @return ターゲット名
        const std::string& GetRenderTargetName() const {
            return targetName_;
        }

        /// @brief バックバッファへ合成する入力論理リソース名を設定
        /// @param name Blackboard 上の論理リソース名
        void SetInputResourceName(const std::string& name) {
            inputResourceName_ = name;
        }

        /// @brief バックバッファへ合成する入力論理リソース名を取得
        /// @return Blackboard 上の論理リソース名
        const std::string& GetInputResourceName() const {
            return inputResourceName_;
        }

    private:
        std::string inputResourceName_ = FrameBlackboard::SceneColor;
        std::string targetName_ = RenderTargetNames::BackBuffer;  ///< デフォルトターゲット名
    };
}

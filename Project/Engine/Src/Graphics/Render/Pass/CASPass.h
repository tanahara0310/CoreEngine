#pragma once
#include "RenderPass.h"
#include <string>

namespace CoreEngine
{
    /// @brief CAS（Contrast Adaptive Sharpening）パス
    /// @details TAA の直後に置き、履歴ブレンドで落ちた解像感を戻す（HDR 空間で動く）。
    /// @note 入力は前段の有無で変わるので、論理名は RenderPipeline が注入する。
    class CASPass : public RenderPass {
    public:
        CASPass() = default;
        ~CASPass() override = default;

        const char* GetName() const override { return "CASPass"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief 補助ビューでは実行しない（シャープ化は最終表示画にのみ意味がある）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void Execute(const RenderContext& context) override;

        /// @brief 入力の論理リソース名を設定する（RenderPipeline が Graph 構築時に呼ぶ）
        void SetInputResourceName(const std::string& name) { inputResourceName_ = name; }
        const std::string& GetInputResourceName() const { return inputResourceName_; }

    private:
        std::string inputResourceName_ = FrameBlackboard::SceneColor;
    };
}

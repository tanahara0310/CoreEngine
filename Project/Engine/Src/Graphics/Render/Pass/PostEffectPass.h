#pragma once
#include "RenderPass.h"
#include <d3d12.h>
#include <string>

namespace CoreEngine
{
    class PostEffectBase;

    /// @brief ポストエフェクト適用パス
    class PostEffectPass : public RenderPass {
    public:
        PostEffectPass() = default;
        ~PostEffectPass() override = default;

        const char* GetName() const override { return "PostEffect"; }

        void SetEffect(PostEffectBase* effect, const std::string& effectName);
        void SetInputResourceName(const std::string& resourceName);
        void SetOutputResourceName(const std::string& resourceName);
        const std::string& GetInputResourceName() const { return inputResourceName_; }
        const std::string& GetOutputResourceName() const { return outputResourceName_; }

        /// @brief エフェクト実行種別に応じた入出力リソースを宣言する
        /// @note effect 未割り当ての placeholder インスタンスは何も宣言しない
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enablePostEffect; }

        void Execute(const RenderContext& context) override;

    private:
        PostEffectBase* effect_ = nullptr;
        std::string effectName_;
        std::string inputResourceName_ = FrameBlackboard::SceneColor;
        std::string outputResourceName_ = FrameBlackboard::SceneColor;
    };
}

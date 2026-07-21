#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    class WaterCausticsPass : public RenderPass
    {
    public:
        WaterCausticsPass() = default;
        ~WaterCausticsPass() override = default;

        const char* GetName() const override { return "WaterCausticsPass"; }

        /// @brief GameView のみ実行する
        /// @details コースティクスは水中の受光面（メインビューでのみ視認される）向けで、
        ///          反射ビューでは水面下がクリップされるため寄与しない。また反射ビューの
        ///          G-Buffer は半解像度のため、フル解像度前提の本パスとは整合しない。
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;

        void SetTargetName(const std::string& name) { targetName_ = name; }

    private:
        std::string targetName_ = RenderTargetNames::WaterCausticsBuffer;
    };
}

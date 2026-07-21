#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    class RTWaterCausticsPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "RTWaterCausticsPass"; }

        /// @brief GameView のみ実行する
        /// @details 本パスは G-Buffer の WorldPosition をフル解像度前提で読むため、
        ///          半解像度 G-Buffer で描画される反射ビューとは整合しない。
        ///          反射内のコースティクス寄与はごく小さいため実行自体を省く。
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}

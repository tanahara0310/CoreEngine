#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    /// @brief 水面専用の forward 合成パス
    /// @details GameView のみで有効。反射ビューで水面を描くと自己反射フィードバックになり、
    ///          波形状の明暗の斑が反射像へ焼き付くため除外している。
    class WaterSurfacePass : public RenderPass {
    public:
        WaterSurfacePass() = default;
        ~WaterSurfacePass() override = default;

        const char* GetName() const override { return "WaterSurface"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief 水面は GameView でのみ描画する（反射ビューへの自己描き込みを防ぐ）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        /// @brief View の SceneColor ターゲット名を出力先へ反映する
        void ConfigureForView(const RenderContext& context) override;

        void Execute(const RenderContext& context) override;

        /// @brief 描画先レンダーターゲットの論理名を設定
        void SetRenderTargetName(const std::string& name) {
            targetName_ = name;
        }

        /// @brief 描画先レンダーターゲットの論理名を取得
        const std::string& GetRenderTargetName() const {
            return targetName_;
        }

    private:
        std::string targetName_ = RenderTargetNames::SceneColor;
    };
}

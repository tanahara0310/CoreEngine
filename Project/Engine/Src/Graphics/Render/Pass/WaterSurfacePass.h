#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    /// @brief 水面専用の forward 合成パス
    /// @details GameView のみで有効（ReflectionView は対象外）。
    ///          反射ビューで水面を描くと、水面が「自分自身の平面反射テクスチャ」へ
    ///          描き込まれる自己反射フィードバックになる。反射カメラは水面の下から
    ///          上を向いており、波で変位した水面は斜めクリップ面（水面高さの平面）を
    ///          またぐため、波形状の塊がそのまま反射像に焼き付く。
    ///          そのタイルは水面自身の色（夜はほぼ黒）なので、本番ビューで
    ///          gReflectionTexture をサンプルすると「波と一緒に動く巨大な明暗の斑」
    ///          として現れる。昼は水面色と空の輝度が近いため目立たず、夜は
    ///          自動露出（最大 +8EV）で黒⇔月明かりの空の最大コントラストになり顕在化する。
    ///          Gerstner / FFTOcean のどちらでも起きる（合成より上流の描画順の問題）。
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

        void SetRenderTargetName(const std::string& name) {
            targetName_ = name;
        }

        const std::string& GetRenderTargetName() const {
            return targetName_;
        }

    private:
        std::string targetName_ = RenderTargetNames::SceneColor;
    };
}

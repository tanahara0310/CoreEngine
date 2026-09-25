#pragma once
#include "../PostEffectGraphicsBase.h"

namespace CoreEngine
{
    /// @brief 画面固定の UI（UI 画像・UI テキスト）を、トーンマップと画面演出の後の色へ重ねる
    /// @details 入力をそのまま写してから UI を描く。フェードとローディング画面はこの後に掛かる
    class UIOverlay : public PostEffectGraphicsBase {
    public:
        /// @brief 入力を写して UI を重ねるパスを積む
        void BuildPasses(PostEffectGraphBuilder& builder) override;

        /// @brief ImGui で説明を表示
        void DrawImGui() override;

        /// @brief 常時有効なエフェクト
        bool IsAlwaysEnabled() const override { return true; }

    protected:
        std::string GetEffectName() const override { return "UIOverlay"; }

        const std::wstring& GetPixelShaderPath() const override;
    };
}

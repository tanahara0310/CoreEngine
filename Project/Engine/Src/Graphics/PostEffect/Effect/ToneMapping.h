#pragma once

#include "../PostEffectBase.h"


namespace CoreEngine
{
/// @brief ACESトーンマッピングポストエフェクト
/// @details HDR→LDR変換をポストエフェクトチェーンの最終段で適用する
class ToneMapping : public PostEffectBase {
public:
    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief トーンマッピングは常時有効。無効化を拒否する。
    void SetEnabled(bool) override { /* 常に有効 */ }

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"ToneMapping.PS.hlsl";
        return pixelShaderPath;
    }
};
}

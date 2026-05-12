#pragma once

#include "../../PostEffectBase.h"
#include <cassert>


namespace CoreEngine
{
/// @brief ACESトーンマッピングポストエフェクト
/// @details HDR→LDR変換をポストエフェクトチェーンの最終段で適用する
class ToneMapping : public PostEffectBase {
public:
    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief トーンマッピングは常時有効。無効化を拒否する。
    void SetEnabled(bool enabled) override { assert(enabled && "ToneMapping cannot be disabled"); }

    /// @brief 常時有効なエフェクト
    bool IsAlwaysEnabled() const override { return true; }

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"ToneMapping.PS.hlsl";
        return pixelShaderPath;
    }
};
}

#pragma once
#include "PostEffectBase.h"


namespace CoreEngine
{
class FullScreen : public PostEffectBase {
public:
    FullScreen() = default;
    ~FullScreen() = default;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief 常時有効なエフェクト
    bool IsAlwaysEnabled() const override { return true; }

protected:
    const std::wstring& GetPixelShaderPath() const override;
};
}

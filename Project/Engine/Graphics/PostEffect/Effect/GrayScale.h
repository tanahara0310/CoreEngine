#pragma once

#include "../PostEffectBase.h"


namespace CoreEngine
{
class GrayScale : public PostEffectBase {
public:
    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"Assets/Shaders/PostProcess/GrayScale.PS.hlsl";
        return pixelShaderPath;
    }
};
}

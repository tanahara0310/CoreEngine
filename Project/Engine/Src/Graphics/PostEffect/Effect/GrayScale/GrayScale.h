#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief グレースケール変換エフェクト（CS方式）
class GrayScale : public PostEffectComputeBase {
public:
public:
    GrayScale() = default;
    ~GrayScale() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

protected:
        /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "GrayScale"; }
    std::wstring GetComputeShaderPath() const override { return L"GrayScale.CS.hlsl"; }


private:

private:
};
}

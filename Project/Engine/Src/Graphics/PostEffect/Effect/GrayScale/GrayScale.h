#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief グレースケール変換エフェクト（CS方式）
class GrayScale : public PostEffectComputeBase {
public:
    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

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
    std::string  GetEffectName()        const override { return "GrayScale"; }
    std::wstring GetComputeShaderPath() const override { return L"GrayScale.CS.hlsl"; }

    /// @brief 定数バッファ生成（基底の InitializeComputeCore から呼ばれる）
    void OnCreateConstantBuffers() override;

private:
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

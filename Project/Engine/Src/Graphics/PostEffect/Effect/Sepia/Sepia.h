#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief セピアエフェクト（CS方式）
class Sepia : public PostEffectComputeBase {
public:
    /// @brief セピアパラメータ構造体
    struct SepiaParams {
        float intensity = 1.0f; // セピア効果の強度 (0.0-2.0)
        float toneRed   = 1.0f; // 赤色調整 (0.5-1.5)
        float toneGreen = 0.8f; // 緑色調整 (0.5-1.5)
        float toneBlue  = 0.6f; // 青色調整 (0.5-1.5)
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    Sepia() = default;
    ~Sepia() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;



    /// @brief 定数バッファを更新
    void UpdateConstantBuffer();

protected:
    /// @brief 有効/無効は CVar "r.Sepia.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "Sepia"; }
    std::wstring GetComputeShaderPath() const override { return L"Sepia.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:

    Microsoft::WRL::ComPtr<ID3D12Resource> sepiaParamsCB_;
    SepiaParams* mappedSepiaParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

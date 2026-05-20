#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief カラーグレーディングエフェクト（CS方式）
class ColorGrading : public PostEffectBase {
public:
    /// @brief カラーグレーディングパラメータ構造体
    struct ColorGradingParams {
        float hue         = 0.0f; // 色相調整 (-1.0 to 1.0)
        float saturation  = 1.0f; // 彩度調整 (0.0 to 3.0)
        float value       = 1.0f; // 明度調整 (0.0 to 3.0)
        float contrast    = 1.0f; // コントラスト (0.0 to 3.0)

        float gamma       = 1.0f; // ガンマ補正 (0.1 to 3.0)
        float temperature = 0.0f; // 色温度 (-1.0 to 1.0)
        float tint        = 0.0f; // ティント (-1.0 to 1.0)
        float exposure    = 0.0f; // 露出調整 (-3.0 to 3.0)

        // Shadow, Midtone, Highlight 調整
        float shadowLift[3]    = { 0.0f, 0.0f, 0.0f };
        float midtoneGamma[3]  = { 1.0f, 1.0f, 1.0f };
        float highlightGain[3] = { 1.0f, 1.0f, 1.0f };
        float padding          = 0.0f;
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    ColorGrading() = default;
    ~ColorGrading() = default;

    /// @brief 初期化（CS用リソース構築）
    void Initialize(DirectXCommon* dxCommon);

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    PostEffectExecutionType GetExecutionType() const override {
        return PostEffectExecutionType::Compute;
    }

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    const ColorGradingParams& GetParams() const { return params_; }
    void SetParams(const ColorGradingParams& params);
    void ApplyPreset(int presetIndex);

protected:
    std::string  GetEffectName()        const override { return "ColorGrading"; }
    std::wstring GetComputeShaderPath() const override { return L"ColorGrading.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    ColorGradingParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> colorGradingParamsCB_;
    ColorGradingParams* mappedColorGradingParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

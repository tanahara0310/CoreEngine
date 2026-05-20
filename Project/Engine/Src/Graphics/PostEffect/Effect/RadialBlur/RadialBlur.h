#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ラジアルブラーエフェクト（CS方式）
class RadialBlur : public PostEffectBase {
public:
    /// @brief ラジアルブラーパラメータ構造体
    struct RadialBlurParams {
        float intensity    = 0.5f; // ブラー強度 (0.0-2.0)
        float sampleCount  = 8.0f; // サンプル数 (4.0-16.0)
        float centerX      = 0.5f; // ブラー中心のX座標 (0.0-1.0)
        float centerY      = 0.5f; // ブラー中心のY座標 (0.0-1.0)
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    RadialBlur() = default;
    ~RadialBlur() = default;

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

    const RadialBlurParams& GetParams() const { return params_; }
    void SetParams(const RadialBlurParams& newParams);

protected:
    std::string GetEffectName() const override { return "RadialBlur"; }

private:
    void CreateComputePipeline();
    void CreateConstantBuffers();
    void UpdateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    RadialBlurParams params_;

    Microsoft::WRL::ComPtr<IDxcBlob>            computeShaderBlob_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;

    Microsoft::WRL::ComPtr<ID3D12Resource> radialBlurParamsCB_;
    RadialBlurParams* mappedRadialBlurParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief セピアエフェクト（CS方式）
class Sepia : public PostEffectBase {
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

    /// @brief パラメータを取得
    const SepiaParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    void SetParams(const SepiaParams& params);

    /// @brief 定数バッファを更新
    void UpdateConstantBuffer();

protected:
    std::string GetEffectName() const override { return "Sepia"; }

private:
    void CreateComputePipeline();
    void CreateConstantBuffers();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    SepiaParams params_;

    Microsoft::WRL::ComPtr<IDxcBlob>            computeShaderBlob_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;

    Microsoft::WRL::ComPtr<ID3D12Resource> sepiaParamsCB_;
    SepiaParams* mappedSepiaParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

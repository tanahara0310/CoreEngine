#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief 色反転エフェクト（CS方式）
class Invert : public PostEffectBase {
public:
    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    Invert() = default;
    ~Invert() = default;

    /// @brief 初期化（CS用リソース構築）
    void Initialize(class DirectXCommon* dxCommon) override;

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

protected:
    std::string GetEffectName() const override { return "Invert"; }

private:
    void CreateComputePipeline();
    void CreateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    Microsoft::WRL::ComPtr<IDxcBlob>            computeShaderBlob_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

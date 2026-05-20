#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ヴィネットエフェクト（CS方式）
class Vignette : public PostEffectBase {
public:
    /// @brief ヴィネットパラメータ構造体
    struct VignetteParams {
        float intensity  = 0.8f;  // ヴィネット強度 (0.0-2.0)
        float smoothness = 0.8f;  // ヴィネットの滑らかさ (0.1-2.0)
        float size       = 16.0f; // ヴィネットサイズ (1.0-50.0)
        float padding    = 0.0f;
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    Vignette() = default;
    ~Vignette() = default;

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

    const VignetteParams& GetParams() const { return params_; }
    void SetParams(const VignetteParams& params);
    void UpdateConstantBuffer();

protected:
    std::string  GetEffectName()        const override { return "Vignette"; }
    std::wstring GetComputeShaderPath() const override { return L"Vignette.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    VignetteParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vignetteParamsCB_;
    VignetteParams* mappedVignetteParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

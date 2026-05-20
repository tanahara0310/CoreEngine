#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ブルームエフェクト（CS方式）
class Bloom : public PostEffectBase {
public:
    /// @brief ブルームパラメータ構造体
    struct BloomParams {
        float threshold  = 0.8f; // 輝度閾値 (0.0-2.0)
        float intensity  = 1.0f; // ブルーム強度 (0.0-3.0)
        float blurRadius = 2.0f; // ブラー半径 (0.5-5.0)
        float softKnee   = 0.5f; // ソフトニー (0.0-1.0)
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    Bloom() = default;
    ~Bloom() = default;

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

    const BloomParams& GetParams() const { return params_; }
    void SetParams(const BloomParams& params);
    void UpdateConstantBuffer();

protected:
    std::string  GetEffectName()        const override { return "Bloom"; }
    std::wstring GetComputeShaderPath() const override { return L"Bloom.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    BloomParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> bloomParamsCB_;
    BloomParams* mappedBloomParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

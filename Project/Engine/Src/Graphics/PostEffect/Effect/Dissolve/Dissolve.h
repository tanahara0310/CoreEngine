#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ディゾルブエフェクト（CS方式）
/// @details ノイズテクスチャを使いピクセルを段階的に消滅させる
class Dissolve : public PostEffectComputeBase {
public:
    /// @brief ディゾルブパラメータ構造体
    struct DissolveParams {
        float threshold  = 0.0f;                  // ディゾルブ閾値 (0.0-1.0)
        float edgeWidth  = 0.1f;                  // エッジ幅 (0.0-0.5)
        float edgeColorR = 1.0f;                  // エッジカラー R
        float edgeColorG = 0.5f;                  // エッジカラー G
        float edgeColorB = 0.0f;                  // エッジカラー B
        float padding[3] = { 0.0f, 0.0f, 0.0f };
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    Dissolve() = default;
    ~Dissolve() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    const DissolveParams& GetParams() const { return params_; }
    void SetParams(const DissolveParams& params);
    void SetThreshold(float threshold);
    void SetEdgeWidth(float width);
    void SetEdgeColor(float r, float g, float b);
    void UpdateConstantBuffer();

protected:
    std::string  GetEffectName()        const override { return "Dissolve"; }
    std::wstring GetComputeShaderPath() const override { return L"Dissolve.CS.hlsl"; }

    /// @brief 定数バッファ生成・ノイズテクスチャ読み込み（両方を一括で実施）
    void OnCreateConstantBuffers() override;

private:
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    DissolveParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveParamsCB_;
    DissolveParams* mappedDissolveParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle_ = {};
};
}

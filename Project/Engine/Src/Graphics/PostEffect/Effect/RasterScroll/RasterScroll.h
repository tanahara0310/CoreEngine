#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ラスタースクロールエフェクト（CS方式）
class RasterScroll : public PostEffectComputeBase {
public:
    /// @brief ラスタースクロールパラメータ構造体
    struct RasterScrollParams {
        float scrollSpeed        = 1.0f;  // スクロール速度 (0.0-10.0)
        float lineHeight         = 10.0f; // ライン高さ (1.0-20.0)
        float amplitude          = 0.02f; // 振幅 (0.0-0.2)
        float frequency          = 1.5f;  // 周波数 (0.1-5.0)

        float time               = 0.0f;  // 時間（アニメーション用）
        float lineOffset         = 0.0f;  // ライン開始位置オフセット (0.0-1.0)
        float distortionStrength = 1.0f;  // 歪み強度 (0.0-3.0)
        float padding            = 0.0f;
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    RasterScroll() = default;
    ~RasterScroll() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief 更新処理
    void Update(float deltaTime);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    const RasterScrollParams& GetParams() const { return params_; }
    void SetParams(const RasterScrollParams& params);

protected:
    std::string  GetEffectName()        const override { return "RasterScroll"; }
    std::wstring GetComputeShaderPath() const override { return L"RasterScroll.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    RasterScrollParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> rasterScrollParamsCB_;
    RasterScrollParams* mappedRasterScrollParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;

    float accumulatedTime_ = 0.0f;
};
}

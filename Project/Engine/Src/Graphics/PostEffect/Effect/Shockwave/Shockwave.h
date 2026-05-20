#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ショックウェーブエフェクト（CS方式）
class Shockwave : public PostEffectComputeBase {
public:
    /// @brief ショックウェーブパラメータ構造体
    struct ShockwaveParams {
        float center[2] = { 0.5f, 0.5f }; // 中心座標 (0-1)
        float time      = 0.0f;            // 時間経過
        float strength  = 0.1f;            // 強度
        float thickness = 0.1f;            // 波の厚さ
        float speed     = 1.0f;            // 波の速度
        float padding[2]= { 0.0f, 0.0f };
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    Shockwave() = default;
    ~Shockwave() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ショックウェーブを開始
    void StartShockwave(float centerX, float centerY);

    /// @brief 更新処理
    void Update(float deltaTime);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    bool IsActive() const { return isActive_; }
    const ShockwaveParams& GetParams() const { return params_; }
    void SetParams(const ShockwaveParams& params);

protected:
    std::string  GetEffectName()        const override { return "Shockwave"; }
    std::wstring GetComputeShaderPath() const override { return L"Shockwave.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    ShockwaveParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> shockwaveParamsCB_;
    ShockwaveParams* mappedShockwaveParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;

    bool  isActive_  = false;
    float maxRadius_ = 1.0f;
};
}

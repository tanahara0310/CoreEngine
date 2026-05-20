#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief 色収差エフェクト（CS方式）
class ChromaticAberration : public PostEffectBase {
public:
    /// @brief 色収差パラメータ構造体
    struct ChromaticAberrationParams {
        float intensity       = 3.0f; // 色収差の強度 (0.0 to 20.0)
        float radialFactor    = 1.0f; // 放射状の強度 (0.0 to 3.0)
        float centerX         = 0.5f; // 中心X座標 (0.0 to 1.0)
        float centerY         = 0.5f; // 中心Y座標 (0.0 to 1.0)

        float distortionScale = 1.0f; // 歪み量のスケール (0.0 to 5.0)
        float falloff         = 1.5f; // 端に向かう強度の減衰 (0.1 to 5.0)
        float samples         = 1.0f; // 未使用（将来の拡張用）
        float padding         = 0.0f;
    };

    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    ChromaticAberration() = default;
    ~ChromaticAberration() = default;

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

    const ChromaticAberrationParams& GetParams() const { return params_; }
    void SetParams(const ChromaticAberrationParams& newParams);
    void ApplyPreset(int presetIndex);

protected:
    std::string  GetEffectName()        const override { return "ChromaticAberration"; }
    std::wstring GetComputeShaderPath() const override { return L"ChromaticAberration.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    ChromaticAberrationParams params_;

    Microsoft::WRL::ComPtr<ID3D12Resource> caParamsCB_;
    ChromaticAberrationParams* mappedCAParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ラジアルブラーエフェクト（CS方式）
/// @details パラメータは CVar（"r.RadialBlur.*"）が唯一の保持者。
///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
class RadialBlur : public PostEffectComputeBase {
public:
    /// @brief ラジアルブラーパラメータ構造体（GPU 定数バッファのレイアウト）
    struct RadialBlurParams {
        float intensity    = 0.5f; // ブラー強度 (0.0-2.0)
        float sampleCount  = 8.0f; // サンプル数 (4.0-16.0)
        float centerX      = 0.5f; // ブラー中心のX座標 (0.0-1.0)
        float centerY      = 0.5f; // ブラー中心のY座標 (0.0-1.0)
    };

    static constexpr Cb::Field kRadialBlurParamsFields[] = {
        CB_FIELD(RadialBlurParams, intensity), CB_FIELD(RadialBlurParams, sampleCount),
        CB_FIELD(RadialBlurParams, centerX), CB_FIELD(RadialBlurParams, centerY),
    };
    CB_VERIFY_LAYOUT(RadialBlurParams, kRadialBlurParamsFields);
    CB_BIND_HLSL(RadialBlurParams, kRadialBlurParamsFields, "RadialBlurParams");

public:
    RadialBlur() = default;
    ~RadialBlur() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "RadialBlur"; }
    std::wstring GetComputeShaderPath() const override { return L"RadialBlur.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> radialBlurParamsCB_;
    RadialBlurParams* mappedRadialBlurParams_ = nullptr;
};
}

#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief カラーグレーディングエフェクト（CS方式）
/// @details パラメータは CVar（"r.ColorGrading.*"）が唯一の保持者。
///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
class ColorGrading : public PostEffectComputeBase {
public:
    /// @brief カラーグレーディングパラメータ構造体（GPU 定数バッファのレイアウト）
    struct ColorGradingParams {
        float hue         = 0.0f; // 色相調整 (-1.0 to 1.0)
        float saturation  = 1.0f; // 彩度調整 (0.0 to 3.0)
        float value       = 1.0f; // 明度調整 (0.0 to 3.0)
        float contrast    = 1.0f; // コントラスト (0.0 to 3.0)

        float gamma       = 1.0f; // ガンマ補正 (0.1 to 3.0)
        float temperature = 0.0f; // 色温度 (-1.0 to 1.0)
        float tint        = 0.0f; // ティント (-1.0 to 1.0)
        float exposure    = 0.0f; // 露出調整 (-3.0 to 3.0)

        // Shadow, Midtone, Highlight 調整
        float shadowLift[3]    = { 0.0f, 0.0f, 0.0f };
        float midtoneGamma[3]  = { 1.0f, 1.0f, 1.0f };
        float highlightGain[3] = { 1.0f, 1.0f, 1.0f };
        float padding          = 0.0f;
    };

public:
    ColorGrading() = default;
    ~ColorGrading() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief グレーディングはトーンカーブ通過前の色に対して行う（UE と同じ順序）ため SceneHDR 段
    /// @note この段で動かすには ColorGrading.CS.hlsl が HDR 対応済みである必要がある
    ///       （最終 saturate の除去・ピボットの中間グレー化）
    PostEffectStage GetStage() const override { return PostEffectStage::SceneHDR; }

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "ColorGrading"; }
    std::wstring GetComputeShaderPath() const override { return L"ColorGrading.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> colorGradingParamsCB_;
    ColorGradingParams* mappedColorGradingParams_ = nullptr;
};
}

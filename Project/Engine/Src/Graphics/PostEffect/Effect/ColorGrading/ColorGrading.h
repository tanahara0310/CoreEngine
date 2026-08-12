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
    /// @note 全フィールドを 16B 境界へ明示的に揃えている。旧レイアウトは float[3] を
    ///       詰めて並べており、HLSL の「float3 は 16B 境界を跨げない」規則と食い違って
    ///       midtoneGamma 以降が 4B ずれていた（既定 OFF のため未発覚だった実バグ）
    struct ColorGradingParams {
        float hue         = 0.0f; // 色相調整 (-1.0 to 1.0)
        float saturation  = 1.0f; // 彩度調整 (0.0 to 3.0)
        float value       = 1.0f; // 明度調整 (0.0 to 3.0)
        float contrast    = 1.0f; // コントラスト (0.0 to 3.0)

        float gamma       = 1.0f; // ガンマ補正 (0.1 to 3.0)
        float exposure    = 0.0f; // 露出調整 (-3.0 to 3.0)
        float padding0[2] = {};

        // ホワイトバランス（Bradford 色順応）行列。CPU で Kelvin/Tint から計算する
        float whiteBalanceRow0[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        float whiteBalanceRow1[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
        float whiteBalanceRow2[4] = { 0.0f, 0.0f, 1.0f, 0.0f };

        // Shadow, Midtone, Highlight 調整（xyz 使用・w はパディング）
        float shadowLift[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };
        float midtoneGamma[4]  = { 1.0f, 1.0f, 1.0f, 0.0f };
        float highlightGain[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
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

    /// @brief WB 行列の再計算判定用（0 初期値は必ず初回計算を走らせる）
    float lastKelvin_ = 0.0f;
    float lastTint_   = -1000.0f;
};
}

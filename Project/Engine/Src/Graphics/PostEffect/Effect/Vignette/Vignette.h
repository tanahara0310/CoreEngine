#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ヴィネットエフェクト（CS方式）
/// @details パラメータは CVar（"r.Vignette.*"）が唯一の保持者。ImGui のスライダーと
///          エディタ設定への保存は CVar 側で自動生成されるため、このクラスには
///          パラメータごとの UI コードもシリアライズコードも無い。
class Vignette : public PostEffectComputeBase {
public:
    /// @brief ヴィネットパラメータ構造体（GPU 定数バッファのレイアウト）
    /// @note 既定値の正は CVar の定義（Vignette.cpp）側。ここはレイアウト定義として持つ
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

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief 現在のパラメータを取得する（CVar から構築して返す）
    /// @note CVar が唯一のソースでキャッシュを持たないため、参照ではなく値を返す
    VignetteParams GetParams() const;

    /// @brief パラメータを一括設定する（プリセット読み込み用）
    /// @details 内部では CVar を更新するため、UI 表示と自動保存にも即座に反映される
    void SetParams(const VignetteParams& params);

    /// @brief CVar の現在値を定数バッファへ書き込む
    void UpdateConstantBuffer();

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "Vignette"; }
    std::wstring GetComputeShaderPath() const override { return L"Vignette.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> vignetteParamsCB_;
    VignetteParams* mappedVignetteParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

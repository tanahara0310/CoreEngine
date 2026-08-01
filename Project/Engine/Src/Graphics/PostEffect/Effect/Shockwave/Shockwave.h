#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ショックウェーブエフェクト（CS方式）
/// @details 調整パラメータは CVar（"r.Shockwave.*"）が唯一の保持者。
///          center / time は StartShockwave 以降の実行時状態なので CVar 化していない。
///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
class Shockwave : public PostEffectComputeBase {
public:
    /// @brief ショックウェーブパラメータ構造体（GPU 定数バッファのレイアウト）
    struct ShockwaveParams {
        float center[2] = { 0.5f, 0.5f }; // 中心座標（発動時に決まる実行時値）
        float time      = 0.0f;            // 時間経過（実行時値）
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

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "Shockwave"; }
    std::wstring GetComputeShaderPath() const override { return L"Shockwave.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> shockwaveParamsCB_;
    ShockwaveParams* mappedShockwaveParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;

    // 発動状態（実行時のみ。保存対象ではない）
    float centerX_   = 0.5f;
    float centerY_   = 0.5f;
    float time_      = 0.0f;
    bool  isActive_  = false;
    float maxRadius_ = 1.0f;
};
}

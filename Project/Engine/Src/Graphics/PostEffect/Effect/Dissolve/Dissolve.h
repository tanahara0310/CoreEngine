#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ディゾルブエフェクト（CS方式）
/// @details ノイズテクスチャを使いピクセルを段階的に消滅させる。
///          パラメータは CVar（"r.Dissolve.*"）が唯一の保持者。
///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
class Dissolve : public PostEffectComputeBase {
public:
    /// @brief ディゾルブパラメータ構造体（GPU 定数バッファのレイアウト）
    struct DissolveParams {
        float threshold  = 0.0f;                  // ディゾルブ閾値 (0.0-1.0)
        float edgeWidth  = 0.1f;                  // エッジ幅 (0.0-0.5)
        float edgeColorR = 1.0f;                  // エッジカラー R
        float edgeColorG = 0.5f;                  // エッジカラー G
        float edgeColorB = 0.0f;                  // エッジカラー B
        float padding[3] = { 0.0f, 0.0f, 0.0f };
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

    /// @brief CVar の現在値を定数バッファへ書き込む
    void UpdateConstantBuffer();

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "Dissolve"; }
    std::wstring GetComputeShaderPath() const override { return L"Dissolve.CS.hlsl"; }

    /// @brief 定数バッファ生成・ノイズテクスチャ読み込み（両方を一括で実施）
    void OnCreateConstantBuffers() override;

private:

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveParamsCB_;
    DissolveParams* mappedDissolveParams_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle_ = {};
};
}

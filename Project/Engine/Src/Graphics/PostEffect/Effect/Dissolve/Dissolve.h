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
    /// @brief ディゾルブパラメータ構造体
    /// @note パディングを 1 つも持たない。GPU へは Cb::Upload がフィールド表を見て
    ///       HLSL のオフセットへ配置するため、C++ 側は素直に並べてよい
    ///       （padding の手打ちが不要 = 置き場所を間違えようがない）。
    struct DissolveParams {
        float threshold  = 0.0f; // ディゾルブ閾値 (0.0-1.0)
        float edgeWidth  = 0.1f; // エッジ幅 (0.0-0.5)
        float edgeColorR = 1.0f; // エッジカラー R
        float edgeColorG = 0.5f; // エッジカラー G
        float edgeColorB = 0.0f; // エッジカラー B
    };

    static constexpr Cb::Field kDissolveParamsFields[] = {
        CB_FIELD(DissolveParams, threshold), CB_FIELD(DissolveParams, edgeWidth),
        CB_FIELD(DissolveParams, edgeColorR), CB_FIELD(DissolveParams, edgeColorG),
        CB_FIELD(DissolveParams, edgeColorB),
    };
    // 転送でオフセットを合わせるので、C++ 側のレイアウト一致は要求しない（型だけ検査する）
    CB_VERIFY_TYPES(DissolveParams, kDissolveParamsFields);
    CB_BIND_HLSL(DissolveParams, kDissolveParamsFields, "DissolveParams");

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
    /// @brief マップ先。構造体ポインタではなく生アドレスで持つ（レイアウトが HLSL 側だけのものなので）
    void* mappedDissolveParams_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle_ = {};
};
}

#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>
#include <cassert>


namespace CoreEngine
{
/// @brief ACESトーンマッピングポストエフェクト（CS方式）
/// @details HDR→LDR変換をポストエフェクトチェーンの最終段で適用する
class ToneMapping : public PostEffectBase {
public:
    /// @brief 画面サイズ定数バッファ構造体
    struct ScreenParams {
        uint32_t screenWidth  = 1280;
        uint32_t screenHeight = 720;
        float    pad[2]       = { 0.0f, 0.0f };
    };

public:
    ToneMapping() = default;
    ~ToneMapping() = default;

    /// @brief 初期化（CS用リソース構築）
    void Initialize(class DirectXCommon* dxCommon) override;

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

    /// @brief トーンマッピングは常時有効。無効化を拒否する。
    void SetEnabled(bool enabled) override { assert(enabled && "ToneMapping cannot be disabled"); }

    /// @brief 常時有効なエフェクト
    bool IsAlwaysEnabled() const override { return true; }

protected:
    std::string GetEffectName() const override { return "ToneMapping"; }

private:
    void CreateComputePipeline();
    void CreateConstantBuffer();
    void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
    Microsoft::WRL::ComPtr<IDxcBlob>            computeShaderBlob_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;

    Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
    ScreenParams* mappedScreenParams_ = nullptr;
};
}

#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class Sepia : public PostEffectBase {
public:
    /// @brief セピアパラメータ構造体
    struct SepiaParams {
        float intensity = 1.0f; // セピア効果の強度 (0.0-2.0)
        float toneRed = 1.0f;   // 赤色調整 (0.5-1.5)
        float toneGreen = 0.8f; // 緑色調整 (0.5-1.5)
        float toneBlue = 0.6f;  // 青色調整 (0.5-1.5)
    };

public:
    Sepia() = default;
    ~Sepia() = default;

    /// @brief 初期化
    void Initialize(DirectXCommon* dxCommon);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const SepiaParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const SepiaParams& params);

    /// @brief 定数バッファを強制的に更新
    void ForceUpdateConstantBuffer();

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"Engine/Assets/Shaders/PostProcess/Sepia.PS.hlsl";
        return pixelShaderPath;
    }

    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    SepiaParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    SepiaParams* mappedData_ = nullptr;
};
}

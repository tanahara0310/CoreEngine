#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class Vignette : public PostEffectBase {
public:
    /// @brief ヴィネットパラメータ構造体
    struct VignetteParams {
        float intensity = 0.8f; // ヴィネット強度 (0.0-2.0)
        float smoothness = 0.8f; // ヴィネットの滑らかさ (0.1-2.0)
        float size = 16.0f; // ヴィネットサイズ (1.0-50.0)
        float padding = 0.0f; // パディング
    };

public:
    Vignette() = default;
    ~Vignette() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const VignetteParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const VignetteParams& params);

    /// @brief 定数バッファを強制的に更新
    void ForceUpdateConstantBuffer();

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"Engine/Assets/Shaders/PostProcess/Vignet.PS.hlsl";
        return pixelShaderPath;
    }

    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    VignetteParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    VignetteParams* mappedData_ = nullptr;
};
}

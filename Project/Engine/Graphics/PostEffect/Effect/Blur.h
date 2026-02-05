#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class Blur : public PostEffectBase {
public:
    /// @brief ブラーパラメータ構造体
    struct BlurParams {
        float intensity = 1.0f; // ブラー強度 (0.0-5.0)
        float kernelSize = 1.0f; // カーネルサイズ (0.5-3.0)
        float padding[2] = { 0.0f, 0.0f }; // パディング
    };

public:
    Blur() = default;
    ~Blur() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const BlurParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const BlurParams& params);

    /// @brief 定数バッファを強制的に更新
    void ForceUpdateConstantBuffer();

protected:
    const std::wstring& GetPixelShaderPath() const override;
    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    BlurParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    BlurParams* mappedData_ = nullptr;
};
}

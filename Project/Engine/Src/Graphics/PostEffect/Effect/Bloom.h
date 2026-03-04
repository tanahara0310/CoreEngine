#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class Bloom : public PostEffectBase {
public:
    /// @brief ブルームパラメータ構造体
    struct BloomParams {
        float threshold = 0.8f;     // 輝度閾値 (0.0-2.0)
        float intensity = 1.0f;     // ブルーム強度 (0.0-3.0)
        float blurRadius = 2.0f;    // ブラー半径 (0.5-5.0)
        float softKnee = 0.5f;      // ソフトニー (0.0-1.0)
    };

public:
    Bloom() = default;
    ~Bloom() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const BloomParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const BloomParams& params);

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
    BloomParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    BloomParams* mappedData_ = nullptr;
};
}

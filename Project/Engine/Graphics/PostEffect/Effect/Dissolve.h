#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
class Dissolve : public PostEffectBase {
public:
    /// @brief ディゾルブパラメータ構造体
    struct DissolveParams {
        float threshold = 0.0f;  // ディゾルブ閾値 (0.0-1.0)
        float edgeWidth = 0.1f;  // エッジ幅 (0.0-0.5)
        float edgeColorR = 1.0f; // エッジカラー R
        float edgeColorG = 0.5f; // エッジカラー G
        float edgeColorB = 0.0f; // エッジカラー B
        float padding[3] = { 0.0f, 0.0f, 0.0f }; // パディング
    };

public:
    Dissolve() = default;
    ~Dissolve() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon) override;

    /// @brief 描画（オーバーライド）
    void Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const DissolveParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const DissolveParams& params);

    /// @brief ディゾルブ閾値を設定
    /// @param threshold 閾値 (0.0-1.0)
    void SetThreshold(float threshold);

    /// @brief エッジ幅を設定
    /// @param width エッジ幅 (0.0-0.5)
    void SetEdgeWidth(float width);

    /// @brief エッジカラーを設定
    /// @param r 赤成分
    /// @param g 緑成分
    /// @param b 青成分
    void SetEdgeColor(float r, float g, float b);

    /// @brief 定数バッファを強制的に更新
    void ForceUpdateConstantBuffer();

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"Assets/Shaders/PostProcess/Dissolve.PS.hlsl";
        return pixelShaderPath;
    }

    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief カスタムルートシグネチャの作成
    void CreateCustomRootSignature();

    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

    /// @brief ノイズテクスチャの読み込み
    void LoadNoiseTexture();

private:
    DissolveParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    DissolveParams* mappedData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE noiseTextureHandle_;
};
}


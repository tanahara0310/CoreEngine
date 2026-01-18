#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class ChromaticAberration : public PostEffectBase {
public:
    /// @brief 色収差パラメータ構造体
    struct ChromaticAberrationParams {
        float intensity = 3.0f;     // 色収差の強度 (0.0 to 20.0)
        float radialFactor = 1.0f;  // 放射状の強度 (0.0 to 3.0)
        float centerX = 0.5f;       // 中心X座標 (0.0 to 1.0)
        float centerY = 0.5f;       // 中心Y座標 (0.0 to 1.0)
        
        float distortionScale = 1.0f; // 歪み量のスケール (0.0 to 5.0)
        float falloff = 1.5f;         // 端に向かう強度の減衰 (0.1 to 5.0)
        float samples = 1.0f;         // 未使用（将来の拡張用）
        float padding = 0.0f;         // パディング調整
    };

public:
    ChromaticAberration() = default;
    ~ChromaticAberration() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const ChromaticAberrationParams& GetParams() const { return params_; }
    
    /// @brief パラメータを設定（GPU転送も含む）
    /// @param newParams 新しいパラメータ
    void SetParams(const ChromaticAberrationParams& newParams);

    /// @brief プリセット適用関数
    void ApplyPreset(int presetIndex);

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"Assets/Shaders/PostProcess/ChromaticAberration.PS.hlsl";
        return pixelShaderPath;
    }

    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    ChromaticAberrationParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    ChromaticAberrationParams* mappedData_ = nullptr;
};
}

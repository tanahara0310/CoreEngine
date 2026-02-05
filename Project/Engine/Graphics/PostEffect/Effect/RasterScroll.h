#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class RasterScroll : public PostEffectBase {
public:
    /// @brief ラスタースクロールパラメータ構造体
    struct RasterScrollParams {
        float scrollSpeed = 1.0f;          // スクロール速度 (0.0-10.0)
        float lineHeight = 10.0f;          // ライン高さ (1.0-20.0)
        float amplitude = 0.02f;           // 振幅 (0.0-0.2)
        float frequency = 1.5f;            // 周波数 (0.1-5.0)
        
        float time = 0.0f;                 // 時間（アニメーション用）
        float lineOffset = 0.0f;           // ライン開始位置オフセット (0.0-1.0)
        float distortionStrength = 1.0f;   // 歪み強度 (0.0-3.0)
        float padding = 0.0f;              // パディング
    };

public:
    RasterScroll() = default;
    ~RasterScroll() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief 更新処理
    /// @param deltaTime フレーム時間
    void Update(float deltaTime);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const RasterScrollParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const RasterScrollParams& params);

protected:
    const std::wstring& GetPixelShaderPath() const override;
    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    RasterScrollParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    RasterScrollParams* mappedData_ = nullptr;
    
    // アニメーション用内部変数
    float accumulatedTime_ = 0.0f;
};
}

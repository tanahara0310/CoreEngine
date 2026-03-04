#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>

/// @brief ショックウェーブポストエフェクトクラス

namespace CoreEngine
{
class Shockwave : public PostEffectBase {
public:
    /// @brief ショックウェーブパラメータ構造体
    struct ShockwaveParams {
        float center[2] = { 0.5f, 0.5f }; // 中心座標 (0-1)
        float time = 0.0f; // 時間経過
        float strength = 0.1f; // 強度
        float thickness = 0.1f; // 波の厚さ
        float speed = 1.0f; // 波の速度
        float padding[2] = { 0.0f, 0.0f }; // パディング
    };

public:
    Shockwave() = default;
    ~Shockwave() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief ショックウェーブを開始
    /// @param centerX 中心X座標 (0-1)
    /// @param centerY 中心Y座標 (0-1)
    void StartShockwave(float centerX, float centerY);

    /// @brief 更新処理
    /// @param deltaTime フレーム時間
    void Update(float deltaTime);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief アクティブかどうか
    /// @return アクティブならtrue
    bool IsActive() const { return isActive_; }

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const ShockwaveParams& GetParams() const { return params_; }

    /// @brief パラメータを設定して定数バッファを更新
    /// @param params 新しいパラメータ
    void SetParams(const ShockwaveParams& params);

protected:
    const std::wstring& GetPixelShaderPath() const override;
    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    ShockwaveParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    ShockwaveParams* mappedData_ = nullptr;
    bool isActive_ = false;
    float maxRadius_ = 1.0f; // 最大半径
};
}

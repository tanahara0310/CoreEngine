#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>

/// @brief フェード効果用ポストエフェクトクラス

namespace CoreEngine
{
class FadeEffect : public PostEffectBase {
public:
    /// @brief フェードのタイプ
    enum class FadeType {
        BlackFade = 0,   // 黒フェード
        WhiteFade = 1,   // 白フェード
        SpiralFade = 2,  // 渦巻きフェード
        RippleFade = 3,  // 波紋フェード
        GlitchFade = 4,  // グリッチフェード
        PortalFade = 5   // ポータルフェード
    };

    /// @brief フェードパラメータ構造体
    struct FadeParams {
        float fadeAlpha = 0.0f;        // フェード強度 (0.0 = 透明, 1.0 = 完全フェード)
        float fadeType = 0.0f;         // フェードタイプ (0:Black, 1:White, 2:Spiral, 3:Ripple, 4:Glitch, 5:Portal)
        float time = 0.0f;             // 時間パラメータ（アニメーション用）
        float spiralPower = 5.0f;      // 渦巻きの強さ
        float rippleFreq = 10.0f;      // 波紋の周波数
        float glitchIntensity = 0.5f;  // グリッチの強さ
        float portalSize = 0.3f;       // ポータルサイズ
        float colorShift = 0.0f;       // 色相シフト
        float padding[2] = { 0.0f, 0.0f }; // 16バイトアライメント用パディング
    };

public:
    FadeEffect() = default;
    ~FadeEffect() = default;

    /// @brief 初期化
    void Initialize(class DirectXCommon* dxCommon);

    /// @brief 更新処理
    /// @param deltaTime フレーム時間
    void Update(float deltaTime);

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief フェード強度を設定
    /// @param alpha フェード強度 (0.0 = 透明, 1.0 = 完全フェード)
    void SetFadeAlpha(float alpha);

    /// @brief フェードタイプを設定（従来の互換性用）
    /// @param fadeToBlack trueなら黒フェード、falseなら白フェード
    void SetFadeType(bool fadeToBlack);

    /// @brief フェードタイプを設定（新機能）
    /// @param type フェードタイプ
    void SetFadeType(FadeType type);

    /// @brief 渦巻きの強さを設定
    /// @param power 渦巻きの強さ
    void SetSpiralPower(float power);

    /// @brief 波紋の周波数を設定
    /// @param frequency 波紋の周波数
    void SetRippleFrequency(float frequency);

    /// @brief グリッチの強さを設定
    /// @param intensity グリッチの強さ
    void SetGlitchIntensity(float intensity);

    /// @brief ポータルサイズを設定
    /// @param size ポータルサイズ
    void SetPortalSize(float size);

    /// @brief 色相シフトを設定
    /// @param shift 色相シフト値
    void SetColorShift(float shift);

    /// @brief フェード強度を取得
    /// @return 現在のフェード強度
    float GetFadeAlpha() const { return params_.fadeAlpha; }

    /// @brief パラメータを取得
    /// @return パラメータ構造体の参照
    const FadeParams& GetParams() const { return params_; }

protected:
    const std::wstring& GetPixelShaderPath() const override
    {
        static const std::wstring pixelShaderPath = L"Assets/Shaders/PostProcess/FadeEffect.PS.hlsl";
        return pixelShaderPath;
    }

    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    /// @brief 定数バッファの作成
    void CreateConstantBuffer();
    
    /// @brief 定数バッファの更新
    void UpdateConstantBuffer();

private:
    FadeParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    FadeParams* mappedData_ = nullptr;
    
    float timeAccumulator_ = 0.0f;  // 時間の累積値
};
}

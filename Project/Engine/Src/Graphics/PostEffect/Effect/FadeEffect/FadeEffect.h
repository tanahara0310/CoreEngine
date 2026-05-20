#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief フェード効果ポストエフェクト（CS方式）
    class FadeEffect : public PostEffectComputeBase {
    public:
        /// @brief フェードのタイプ
        enum class FadeType {
            BlackFade = 0,
            WhiteFade = 1,
            SpiralFade = 2,
            RippleFade = 3,
            GlitchFade = 4,
            PortalFade = 5
        };

        /// @brief フェードパラメータ構造体
        struct FadeParams {
            float fadeAlpha = 0.0f; // フェード強度 (0.0 = 透明, 1.0 = 完全フェード)
            float fadeType = 0.0f; // フェードタイプ
            float time = 0.0f; // 時間パラメータ
            float spiralPower = 5.0f; // 渦巻きの強さ
            float rippleFreq = 10.0f;// 波紋の周波数
            float glitchIntensity = 0.5f; // グリッチの強さ
            float portalSize = 0.3f; // ポータルサイズ
            float colorShift = 0.0f; // 色相シフト
            float padding[2] = { 0.0f, 0.0f };
        };

        /// @brief 画面サイズ定数バッファ構造体
        struct ScreenParams {
            uint32_t screenWidth = 1280;
            uint32_t screenHeight = 720;
            float    pad[2] = { 0.0f, 0.0f };
        };

    public:
        FadeEffect() = default;
        ~FadeEffect() = default;

        /// @brief CSエフェクト実行
        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        /// @brief 更新処理
        void Update(float deltaTime);

        /// @brief ImGuiでパラメータを調整
        void DrawImGui() override;

        void SetFadeAlpha(float alpha);
        void SetFadeType(bool fadeToBlack);
        void SetFadeType(FadeType type);
        void SetSpiralPower(float power);
        void SetRippleFrequency(float frequency);
        void SetGlitchIntensity(float intensity);
        void SetPortalSize(float size);
        void SetColorShift(float shift);

        float GetFadeAlpha() const { return params_.fadeAlpha; }
        const FadeParams& GetParams() const { return params_; }

    protected:
        std::string  GetEffectName()        const override { return "FadeEffect"; }
        std::wstring GetComputeShaderPath() const override { return L"FadeEffect.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateConstantBuffer();
        void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

    private:
        FadeParams params_;

        Microsoft::WRL::ComPtr<ID3D12Resource> fadeParamsCB_;
        FadeParams* mappedFadeParams_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
        ScreenParams* mappedScreenParams_ = nullptr;

        float timeAccumulator_ = 0.0f;
    };
}

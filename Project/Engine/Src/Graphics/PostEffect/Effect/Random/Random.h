#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief ランダムノイズエフェクト（CS方式）
    class Random : public PostEffectComputeBase {
    public:
        /// @brief ランダムノイズパラメータ構造体
        struct RandomParams {
            float intensity = 0.15f; // ノイズ強度 (0.0-1.0)
            float blend = 0.35f; // 元画像とのブレンド率 (0.0-1.0)
            float speed = 1.0f;  // 時間変化速度 (0.0-10.0)
            float time = 0.0f;  // 時間

            float grainScale = 1.0f;  // 粒度スケール (0.1-8.0)
            float luminanceInfluence = 0.25f; // 輝度への影響度 (0.0-1.0)
            float chromaAmount = 0.15f; // 色ノイズ量 (0.0-1.0)
            float padding = 0.0f;
        };

        /// @brief 画面サイズ定数バッファ構造体
        struct ScreenParams {
            uint32_t screenWidth = 1280;
            uint32_t screenHeight = 720;
            float    pad[2] = { 0.0f, 0.0f };
        };

    public:
        Random() = default;
        ~Random() = default;

        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        void Update(float deltaTime) override;
        void DrawImGui() override;

        const RandomParams& GetParams() const { return params_; }
        void SetParams(const RandomParams& params);

    protected:
        std::string  GetEffectName()        const override { return "Random"; }
        std::wstring GetComputeShaderPath() const override { return L"Random.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateConstantBuffer();
        void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

    private:
        RandomParams params_;

        Microsoft::WRL::ComPtr<ID3D12Resource> randomParamsCB_;
        RandomParams* mappedRandomParams_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
        ScreenParams* mappedScreenParams_ = nullptr;

        float accumulatedTime_ = 0.0f;
    };
}

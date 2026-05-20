#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief ガウシアンブラー
    class Blur : public PostEffectBase {
    public:
        /// @brief ブラーパラメータ構造体
        struct BlurParams {
            float intensity = 1.0f;         // ブラー強度 (0.0-5.0)
            float kernelSize = 1.0f;         // カーネルサイズ (0.5-3.0)
            float padding[2] = { 0.0f, 0.0f };
        };

        /// @brief 画面サイズ定数バッファ構造体
        struct ScreenParams {
            uint32_t screenWidth = 1280;
            uint32_t screenHeight = 720;
            float    pad[2] = { 0.0f, 0.0f };
        };

    public:
        Blur() = default;
        ~Blur() = default;

        /// @brief 初期化（CS用リソース構築）
        void Initialize(class DirectXCommon* dxCommon) override;

        /// @brief CSエフェクト実行
        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        PostEffectExecutionType GetExecutionType() const override {
            return PostEffectExecutionType::Compute;
        }

        /// @brief ImGuiでパラメータを調整
        void DrawImGui() override;

        /// @brief パラメータを取得
        const BlurParams& GetParams() const { return params_; }

        /// @brief パラメータを設定して定数バッファを更新
        void SetParams(const BlurParams& params);

    protected:
        std::string  GetEffectName()        const override { return "Blur"; }
        std::wstring GetComputeShaderPath() const override { return L"Blur.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateBlurConstantBuffer();
        void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

    private:
        BlurParams params_;
        ScreenParams screenParams_;

        Microsoft::WRL::ComPtr<ID3D12Resource> blurParamsCB_;
        BlurParams* mappedBlurParams_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
        ScreenParams* mappedScreenParams_ = nullptr;
    };
}

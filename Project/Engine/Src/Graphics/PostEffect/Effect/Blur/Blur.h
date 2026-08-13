#pragma once
#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief ガウシアンブラー
    /// @details パラメータは CVar（"r.Blur.*"）が唯一の保持者。
    ///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
    class Blur : public PostEffectComputeBase {
    public:
        /// @brief ブラーパラメータ構造体（GPU 定数バッファのレイアウト）
        struct BlurParams {
            float intensity = 1.0f;         // ブラー強度 (0.0-5.0)
            float kernelSize = 1.0f;         // カーネルサイズ (0.5-3.0)
            float padding[2] = { 0.0f, 0.0f };
        };

        static constexpr Cb::Field kBlurParamsFields[] = {
            CB_FIELD(BlurParams, intensity), CB_FIELD(BlurParams, kernelSize), CB_FIELD(BlurParams, padding),
        };
        CB_VERIFY_LAYOUT(BlurParams, kBlurParamsFields);
        // HLSL 側の照合（CB_BIND_HLSL）は入れていない。"BlurParams" という cbuffer 名を
        // Blur.CS.hlsl（本構造体）と LocalExposureBlur.CS.hlsl（uint2 textureSize / uint2 direction）が
        // 別レイアウトで共用しており、名前だけではどちらを指すか決められないため。
        // 照合を有効にしたい場合は HLSL 側の cbuffer 名を分けること。

    public:
        Blur() = default;
        ~Blur() = default;

        /// @brief CSエフェクト実行
        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        /// @brief ImGuiでパラメータを調整
        void DrawImGui() override;

    protected:
        /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string  GetEffectName()        const override { return "Blur"; }
        std::wstring GetComputeShaderPath() const override { return L"Blur.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateBlurConstantBuffer();

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> blurParamsCB_;
        BlurParams* mappedBlurParams_ = nullptr;
    };
}

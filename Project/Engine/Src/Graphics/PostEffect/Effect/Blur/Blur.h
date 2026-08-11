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

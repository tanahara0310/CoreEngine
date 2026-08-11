#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief 色反転エフェクト（CS方式）
    class Invert : public PostEffectComputeBase {
    public:
    public:
        Invert() = default;
        ~Invert() = default;

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

        std::string  GetEffectName() const override { return "Invert"; }
        std::wstring GetComputeShaderPath() const override { return L"Invert.CS.hlsl"; }

    private:

    private:
    };
}

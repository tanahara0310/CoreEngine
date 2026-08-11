#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief ランダムノイズエフェクト（CS方式）
    /// @details 調整パラメータは CVar（"r.Random.*"）が唯一の保持者。
    ///          time だけは実行時に累積される値なので CVar 化していない。
    ///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
    class Random : public PostEffectComputeBase {
    public:
        /// @brief ランダムノイズパラメータ構造体（GPU 定数バッファのレイアウト）
        struct RandomParams {
            float intensity = 0.15f; // ノイズ強度 (0.0-1.0)
            float blend = 0.35f; // 元画像とのブレンド率 (0.0-1.0)
            float speed = 1.0f;  // 時間変化速度 (0.0-10.0)
            float time = 0.0f;  // 時間（実行時に累積。CVar 化していない）

            float grainScale = 1.0f;  // 粒度スケール (0.1-8.0)
            float luminanceInfluence = 0.25f; // 輝度への影響度 (0.0-1.0)
            float chromaAmount = 0.15f; // 色ノイズ量 (0.0-1.0)
            float padding = 0.0f;
        };

    public:
        Random() = default;
        ~Random() = default;

        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        void PrepareFrame(const PostEffectFrameContext& ctx) override;
        void DrawImGui() override;

    protected:
        /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string  GetEffectName()        const override { return "Random"; }
        std::wstring GetComputeShaderPath() const override { return L"Random.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateConstantBuffer();

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> randomParamsCB_;
        RandomParams* mappedRandomParams_ = nullptr;

        float accumulatedTime_ = 0.0f;
    };
}

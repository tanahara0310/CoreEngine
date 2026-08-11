#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief フェード効果ポストエフェクト（CS方式）
    /// @details 演出の調整パラメータは CVar（"r.Fade.*"）が唯一の保持者。
    ///          fadeAlpha / fadeType / time は SceneTransition が制御する実行時状態のため
    ///          CVar 化していない（保存すると黒画面で起動する事故になる）。
    ///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
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

        /// @brief フェードパラメータ構造体（GPU 定数バッファのレイアウト）
        struct FadeParams {
            float fadeAlpha = 0.0f; // フェード強度（実行時値）
            float fadeType = 0.0f; // フェードタイプ（実行時値）
            float time = 0.0f; // 時間パラメータ（実行時値）
            float spiralPower = 5.0f; // 渦巻きの強さ
            float rippleFreq = 10.0f;// 波紋の周波数
            float glitchIntensity = 0.5f; // グリッチの強さ
            float portalSize = 0.3f; // ポータルサイズ
            float colorShift = 0.0f; // 色相シフト
            float padding[2] = { 0.0f, 0.0f };
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
        void PrepareFrame(const PostEffectFrameContext& ctx) override;

        /// @brief ImGuiでパラメータを調整
        void DrawImGui() override;

        /// @brief フェードの進行度を設定する（SceneTransition が毎フレーム呼ぶ）
        void SetFadeAlpha(float alpha);

        /// @brief フェードの種類を設定する
        void SetFadeType(FadeType type);

    protected:
        /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string  GetEffectName()        const override { return "FadeEffect"; }
        std::wstring GetComputeShaderPath() const override { return L"FadeEffect.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateConstantBuffer();

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> fadeParamsCB_;
        FadeParams* mappedFadeParams_ = nullptr;

        // 実行時状態（保存対象ではない）
        float fadeAlpha_ = 0.0f;
        float fadeType_ = 0.0f;
        float timeAccumulator_ = 0.0f;
    };
}

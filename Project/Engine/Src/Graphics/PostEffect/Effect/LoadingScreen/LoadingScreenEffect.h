#pragma once

#include "../PostEffectComputeBase.h"
#include "Math/Vector/Vector4.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
    /// @brief ローディング画面ポストエフェクト（CS方式）
    /// @details 見た目は CVar（"r.Loading.*"）が保持し、表示強度は SceneTransition が毎フレーム設定する。
    class LoadingScreenEffect : public PostEffectComputeBase {
    public:
        /// @brief ローディング画面パラメータ構造体（GPU 定数バッファのレイアウト）
        struct LoadingParams {
            float screenAlpha = 0.0f;  // 表示強度（実行時値）
            float time = 0.0f;  // 経過時間（実行時値）
            float speed = 0.75f;
            float spring = 2.30f;

            float stepTurn = 0.70f;
            float arcCount = 3.0f;
            float arcLength = 0.37f;
            float arcPulse = 0.19f;

            float thickness = 10.0f;
            float radius = 39.0f;
            float tipDot = 1.0f;
            float trackEnabled = 1.0f;

            float hueCycle = 1.0f;
            float centerX = 0.5f;
            float centerY = 0.5f;
            float trackAlpha = 1.0f;

            float progress = 0.0f;    // 読み込みの進捗（実行時値）
            float gaugeAlpha = 0.0f;  // 進捗ゲージの表示強度（実行時値）
            float gaugePad0 = 0.0f;
            float gaugePad1 = 0.0f;

            Vector4 arcColor0 = { 1.0f, 0.365f, 0.561f, 1.0f };
            Vector4 arcColor1 = { 0.208f, 0.816f, 0.847f, 1.0f };
            Vector4 arcColor2 = { 1.0f, 0.788f, 0.235f, 1.0f };
            Vector4 trackColor = { 0.141f, 0.157f, 0.200f, 1.0f };
        };

        static constexpr Cb::Field kLoadingParamsFields[] = {
            CB_FIELD(LoadingParams, screenAlpha), CB_FIELD(LoadingParams, time),
            CB_FIELD(LoadingParams, speed), CB_FIELD(LoadingParams, spring),
            CB_FIELD(LoadingParams, stepTurn), CB_FIELD(LoadingParams, arcCount),
            CB_FIELD(LoadingParams, arcLength), CB_FIELD(LoadingParams, arcPulse),
            CB_FIELD(LoadingParams, thickness), CB_FIELD(LoadingParams, radius),
            CB_FIELD(LoadingParams, tipDot), CB_FIELD(LoadingParams, trackEnabled),
            CB_FIELD(LoadingParams, hueCycle), CB_FIELD(LoadingParams, centerX),
            CB_FIELD(LoadingParams, centerY), CB_FIELD(LoadingParams, trackAlpha),
            CB_FIELD(LoadingParams, progress), CB_FIELD(LoadingParams, gaugeAlpha),
            CB_FIELD(LoadingParams, gaugePad0), CB_FIELD(LoadingParams, gaugePad1),
            CB_FIELD(LoadingParams, arcColor0), CB_FIELD(LoadingParams, arcColor1),
            CB_FIELD(LoadingParams, arcColor2), CB_FIELD(LoadingParams, trackColor),
        };
        CB_VERIFY_LAYOUT(LoadingParams, kLoadingParamsFields);
        CB_BIND_HLSL(LoadingParams, kLoadingParamsFields, "LoadingParams");

    public:
        LoadingScreenEffect() = default;
        ~LoadingScreenEffect() = default;

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

        /// @brief 表示強度を設定する（SceneTransition が毎フレーム呼ぶ）
        void SetScreenAlpha(float alpha);

        /// @brief 読み込みの進捗を設定する
        void SetProgress(float progress);

        /// @brief 進捗ゲージの表示強度を設定する
        void SetGaugeAlpha(float alpha);

    protected:
        /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string  GetEffectName()        const override { return "LoadingScreen"; }
        std::wstring GetComputeShaderPath() const override { return L"LoadingScreen.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateConstantBuffer();

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> loadingParamsCB_;
        LoadingParams* mappedLoadingParams_ = nullptr;

        // 実行時状態（保存対象ではない）
        float screenAlpha_ = 0.0f;
        float timeAccumulator_ = 0.0f;
        float progress_ = 0.0f;
        float gaugeAlpha_ = 0.0f;
    };
}

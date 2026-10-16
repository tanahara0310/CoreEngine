#pragma once
#include "../PostEffectComputeBase.h"
#include "../ILoadingScreenEffect.h"

#include <array>
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief プロジェクトのコンピュートシェーダで描くロード画面
    /// @details シェーダと画像はプロジェクト設定（Project.json の `loadingScreenShader`・`loadingScreenImages`）が決める。
    ///          シェーダへ渡すもの：
    ///          - `Texture2D<float4> gTexture : register(t0)` … 合成前の画面
    ///          - `RWTexture2D<float4> gOutput : register(u0)` … 書き出し先
    ///          - `Texture2D<float4> gImage0〜gImage7 : register(t1〜t8)` … 設定の画像（並び順）
    ///          - `cbuffer ProjectLoadingParams : register(b0)` … 表示の強さ・時間・進み具合
    ///          - `cbuffer ScreenParams : register(b1)` … 画面の大きさ
    ///          スレッドグループは 8×8。
    class ProjectLoadingScreen : public PostEffectComputeBase, public ILoadingScreenEffect {
    public:
        /// @brief シェーダへ渡せる画像の数
        static constexpr size_t kMaxImages = 8;

        /// @brief ロード画面の定数（GPU 定数バッファのレイアウト）
        struct ProjectLoadingParams {
            float screenAlpha = 0.0f; // 表示の強さ（0 = 出さない ／ 1 = 出し切る）
            float time = 0.0f;        // 出し始めからの時間（1 フレーム 0.1 秒まで）
            float realTime = 0.0f;    // 出し始めからの実時間
            float progress = 0.0f;    // 読み込みの進み具合（0〜1。なめらかに追う）
            float gaugeAlpha = 0.0f;  // 進み具合を出す強さ（読み込みが長いときだけ 1 へ）
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };
        static constexpr Cb::Field kProjectLoadingParamsFields[] = {
            CB_FIELD(ProjectLoadingParams, screenAlpha), CB_FIELD(ProjectLoadingParams, time),
            CB_FIELD(ProjectLoadingParams, realTime), CB_FIELD(ProjectLoadingParams, progress),
            CB_FIELD(ProjectLoadingParams, gaugeAlpha), CB_FIELD(ProjectLoadingParams, pad0),
            CB_FIELD(ProjectLoadingParams, pad1), CB_FIELD(ProjectLoadingParams, pad2),
        };
        CB_VERIFY_LAYOUT(ProjectLoadingParams, kProjectLoadingParamsFields);
        CB_BIND_HLSL(ProjectLoadingParams, kProjectLoadingParamsFields, "ProjectLoadingParams");

    public:
        ProjectLoadingScreen() = default;
        ~ProjectLoadingScreen() = default;

        /// @brief プロジェクト設定がロード画面のシェーダを決めているか
        static bool IsConfigured();

        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        /// @brief 時間を進め、進み具合をなめらかに追う
        void PrepareFrame(const PostEffectFrameContext& ctx) override;

        void DrawImGui() override;

        // ---- ILoadingScreenEffect ----
        void SetScreenAlpha(float alpha) override;
        void SetProgress(float progress) override;
        void SetGaugeAlpha(float alpha) override;
        void SetLoadingEnabled(bool enabled) override { SetEnabled(enabled); }

    protected:
        CVar<bool>* GetEnabledCVar() const override;
        std::string  GetEffectName() const override { return "ProjectLoadingScreen"; }
        std::wstring GetComputeShaderPath() const override;
        /// @brief 設定の画像を読み込む（無い枠には白い 1×1 を入れる）
        void OnCreateResources() override;

    private:
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMaxImages> images_{};

        float screenAlpha_ = 0.0f;
        float time_ = 0.0f;
        float realTime_ = 0.0f;
        float progress_ = 0.0f;
        float progressTarget_ = 0.0f;
        float gaugeAlpha_ = 0.0f;
    };
}

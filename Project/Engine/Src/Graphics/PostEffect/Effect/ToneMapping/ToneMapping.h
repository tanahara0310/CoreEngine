#pragma once

#include "Graphics/RHI/Resource/GpuResource.h"

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>
#include <cassert>
#include <algorithm>


namespace CoreEngine
{
    /// @brief ACES トーンマッピングポストエフェクト（CS 方式）
    /// @details HDR → LDR 変換を担う「段の境界」。これより前が SceneHDR 段、後ろが PostTonemap 段。
    /// @note 自動露出は対数平均輝度から毎フレーム計算し、手動の露出補正 [EV] はその加算オフセット。
    class ToneMapping : public PostEffectComputeBase {
    public:
        /// @brief 画面サイズ定数バッファ構造体
        /// @note サイズを変えるとクリーンビルドが要る（ODR 事故の前科）。pad を使い切っている
        struct ScreenParams {
            uint32_t screenWidth = 1280;
            uint32_t screenHeight = 720;
            float exposureEV = 0.0f;      ///< 露出補正 [EV]。トーンカーブ適用前に exp2(EV) を乗算（0 = 従来動作）
            uint32_t toneMapOperator = 0; ///< 0=ACES / 1=GT / 2=AgX
        };

        static constexpr Cb::Field kScreenParamsFields[] = {
            CB_FIELD(ScreenParams, screenWidth), CB_FIELD(ScreenParams, screenHeight),
            CB_FIELD(ScreenParams, exposureEV), CB_FIELD(ScreenParams, toneMapOperator),
        };
        CB_VERIFY_LAYOUT(ScreenParams, kScreenParamsFields);

        /// @brief ヒストグラム測光の定数バッファ構造体（LuminanceReduction.CS の b1）
        struct HistogramMeteringParams {
            float lowPercentile = 0.5f;  ///< この割合より暗いサンプルを捨てる
            float highPercentile = 0.9f; ///< この割合より明るいサンプルを捨てる
            float pad[2] = {};
        };

        static constexpr Cb::Field kHistogramMeteringParamsFields[] = {
            CB_FIELD(HistogramMeteringParams, lowPercentile), CB_FIELD(HistogramMeteringParams, highPercentile),
            CB_FIELD(HistogramMeteringParams, pad),
        };
        CB_VERIFY_LAYOUT(HistogramMeteringParams, kHistogramMeteringParamsFields);

    public:
        ToneMapping() = default;
        ~ToneMapping() = default;

        /// @brief CSエフェクト実行
        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        /// @brief ImGuiでパラメータを調整
        void DrawImGui() override;

        /// @brief トーンマッピングは常時有効。無効化を拒否する。
        void SetEnabled([[maybe_unused]] bool enabled) override { assert(enabled && "ToneMapping cannot be disabled"); }

        /// @brief 常時有効なエフェクト
        bool IsAlwaysEnabled() const override { return true; }

        /// @brief HDR→LDR の境界そのもの。チェーン中ちょうど 1 つだけ存在する
        PostEffectStage GetStage() const override { return PostEffectStage::Tonemap; }

        /// @brief 自動露出の有効/無効を設定する
        /// @note AtmosphereEditor が夜間プリセットで一時的に切り替えるため公開している
        void SetAutoExposureEnabled(bool enabled);

        /// @brief 自動露出が有効か
        bool IsAutoExposureEnabled() const;

        /// @brief 照明駆動測光用のシーン代表輝度を設定する（大気システムが毎フレーム呼ぶ）
        /// @details 太陽・月の照明状態から解析されたカメラ非依存の輝度。
        ///          毎フレーム呼ばれている間だけ照明駆動測光が機能し、呼ばれないフレームは
        ///          画面平均測光へ自動フォールバックする（大気の無いシーン対応）。
        void SetSceneIlluminationLuminance(float luminance) {
            illuminationLuminance_ = luminance;
            illuminationValid_ = true;
        }

        /// @brief 現在の自動露出EV（自動露出無効時は 0）
        float GetAutoExposureEV() const;

        /// @brief 現在の順応輝度を基準輝度に設定する（「今の明るさを 0EV にする」操作）
        void CalibrateReferenceToCurrent();

        /// @brief 時間順応に使うデルタタイムを取り込む
        void PrepareFrame(const PostEffectFrameContext& ctx) override { deltaTime_ = ctx.deltaTime; }

    protected:
        std::string  GetEffectName() const override { return "ToneMapping"; }
        std::wstring GetComputeShaderPath() const override { return L"ToneMapping.CS.hlsl"; }
        void OnCreateConstantBuffers() override;

    private:
        void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

        // ===== 自動露出（Auto Exposure） =====

        /// @brief 輝度計測パイプラインとバッファ群を構築する（失敗しても致命的でない）
        void CreateAutoExposureResources();

        /// @brief 過去フレームの計測値を読み取り、時間順応と自動EVの計算を進める（CPU側）
        void UpdateAutoExposureAdaptation();

        /// @brief 入力テクスチャの平均対数輝度の計測をコマンドリストへ記録する
        void RecordLuminanceReduction(
            ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

        /// @brief 順応輝度に対するターゲットキーを返す（Krawczyk 自動キー）
        /// @details 自動EVの基準点を求めるときにも同じ式を使う必要があるため関数に切り出している
        /// @param luminance 順応輝度
        /// @return ターゲットキー
        float KeyForLuminance(float luminance) const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
        ScreenParams* mappedScreenParams_ = nullptr;

        // ----- 自動露出: 輝度計測パイプライン -----
        Microsoft::WRL::ComPtr<IDxcBlob> reductionShaderBlob_;
        std::unique_ptr<RootSignatureManager> reductionRootSignature_;
        std::unique_ptr<ShaderReflectionData> reductionReflection_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> reductionPso_;

        // ----- 自動露出: 計測バッファ -----
        Microsoft::WRL::ComPtr<ID3D12Resource> histogramParamsCB_; ///< 百分位カット設定（b1）
        HistogramMeteringParams* mappedHistogramParams_ = nullptr;
        GpuResource avgLogLumBuffer_; ///< 測光結果の輝度（DEFAULT/UAV・1要素。ステート追跡込み）
        static constexpr uint32_t kReadbackCount = 3; ///< リードバックリング数（GPU遅延2フレームまで安全）
        Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffers_[kReadbackCount];
        const float* mappedReadback_[kReadbackCount] = {};
        uint64_t reductionFrameCounter_ = 0;
        bool autoExposureReady_ = false; ///< 計測リソースの構築に成功したか

        // ----- 自動露出: 順応状態（調整値は CVar "r.AutoExposure.*" 側が持つ） -----
        float illuminationLuminance_ = 0.18f; ///< 大気システムから供給されるシーン代表輝度
        bool illuminationValid_ = false;      ///< 今フレームに供給があったか（無ければ画面平均へフォールバック）
        bool illuminationActiveLastFrame_ = false; ///< 直近の Dispatch で照明駆動が実際に使われたか（診断表示用）
        float currentKey_ = 0.18f;         ///< 現在のターゲットキー（診断表示用）
        float adaptedLuminance_ = 0.18f;   ///< 順応済み輝度（時間追従する）
        bool adaptationInitialized_ = false;
        float autoEV_ = 0.0f;              ///< 計算された自動露出EV
        float deltaTime_ = 1.0f / 60.0f;
    };
}

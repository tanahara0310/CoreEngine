#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>
#include <cassert>


namespace CoreEngine
{
    /// @brief ACESトーンマッピングポストエフェクト（CS方式）
    /// @details HDR→LDR変換をポストエフェクトチェーンの最終段で適用する。
    ///          自動露出（Auto Exposure）を有効にすると、入力のリニアHDR輝度の
    ///          対数平均から露出を毎フレーム計算し、目の明暗順応のように時間追従する。
    ///          手動の露出補正 [EV] は自動露出への加算オフセットとして機能する。
    class ToneMapping : public PostEffectComputeBase {
    public:
        /// @brief 画面サイズ定数バッファ構造体
        struct ScreenParams {
            uint32_t screenWidth = 1280;
            uint32_t screenHeight = 720;
            float exposureEV = 0.0f;   ///< 露出補正 [EV]。ACES 適用前に exp2(EV) を乗算（0 = 従来動作）
            float pad = 0.0f;
        };

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

        /// @brief 露出補正 [EV] を設定する（+1 で明るさ2倍、-1 で半分）
        /// @details 自動露出が有効な場合は自動EVへの加算オフセットになる
        void SetExposureEV(float ev) { exposureEV_ = ev; }

        /// @brief 露出補正 [EV] を取得する
        float GetExposureEV() const { return exposureEV_; }

        /// @brief 自動露出の有効/無効を設定する
        void SetAutoExposureEnabled(bool enabled) { autoExposureEnabled_ = enabled; }

        /// @brief 自動露出が有効か
        bool IsAutoExposureEnabled() const { return autoExposureEnabled_; }

        /// @brief 現在の自動露出EV（自動露出無効時は 0）
        float GetAutoExposureEV() const { return autoExposureEnabled_ ? autoEV_ : 0.0f; }

        /// @brief 時間順応の更新用にデルタタイムを受け取る（PostEffectManager から毎フレーム呼ばれる）
        void Update(float deltaTime) override { deltaTime_ = deltaTime; }

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

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
        ScreenParams* mappedScreenParams_ = nullptr;
        float exposureEV_ = 0.0f; ///< 露出補正 [EV]（自動露出有効時は加算オフセット）

        // ----- 自動露出: 輝度計測パイプライン -----
        Microsoft::WRL::ComPtr<IDxcBlob> reductionShaderBlob_;
        std::unique_ptr<RootSignatureManager> reductionRootSignature_;
        std::unique_ptr<ShaderReflectionData> reductionReflection_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> reductionPso_;

        // ----- 自動露出: 計測バッファ -----
        Microsoft::WRL::ComPtr<ID3D12Resource> avgLogLumBuffer_; ///< 平均対数輝度（DEFAULT/UAV・1要素）
        static constexpr uint32_t kReadbackCount = 3; ///< リードバックリング数（GPU遅延2フレームまで安全）
        Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffers_[kReadbackCount];
        const float* mappedReadback_[kReadbackCount] = {};
        uint64_t reductionFrameCounter_ = 0;
        bool autoExposureReady_ = false; ///< 計測リソースの構築に成功したか

        // ----- 自動露出: パラメータと順応状態 -----
        bool autoExposureEnabled_ = false; ///< 既定OFF（有効化はImGuiか SetAutoExposureEnabled）
        /// @brief 明暗の絶対感を保持する（Krawczyk 自動キー）
        /// @details false だと全シーンを中間グレーへ正規化してしまい、薄暮の空が昼のように明るくなる
        bool preserveSceneBrightness_ = true;
        float currentKey_ = 0.18f;         ///< 現在のターゲットキー（診断表示用）
        float keyValue_ = 0.18f;           ///< 順応輝度をこの値（中間グレー）へ写す（自動キー時は相対倍率）
        float minAutoEV_ = -4.0f;          ///< 自動EVの下限（真っ暗な画面で増幅しすぎない）
        float maxAutoEV_ = 4.0f;           ///< 自動EVの上限（真っ白な画面で絞りすぎない）
        float adaptationSpeed_ = 1.5f;     ///< 明暗順応の速さ [1/s]
        float adaptedLuminance_ = 0.18f;   ///< 順応済み輝度（時間追従する）
        bool adaptationInitialized_ = false;
        float autoEV_ = 0.0f;              ///< 計算された自動露出EV
        float deltaTime_ = 1.0f / 60.0f;
    };
}

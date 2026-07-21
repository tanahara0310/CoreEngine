#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

#include <array>
#include <cstdint>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;

    /// @brief FFTベースの海面シミュレーションを実行し、変位/法線/ヤコビアンを生成する管理クラス
    class FFTOceanManager
    {
    public:
        /// @brief 海面スペクトル生成と時間発展で使用する入力設定
        struct Settings {
            uint32_t resolution = 256;
            float patchLength = 96.0f;
            float amplitudeScale = 1.0f;
            float windDirection[2] = { 0.92f, 0.38f };
            // 波高は Pierson-Moskowitz 較正（Hs ≈ 0.21 v²/g）で風速から決まる。
            // 12 m/s → 有義波高約 3m の「うねりのある外洋」相当を既定とする。
            float windSpeed = 12.0f;
            float choppiness = 1.35f;
            uint32_t activeComponentCount = 32;
            float gravity = 9.81f;
        };

        /// @brief 必要なGPUリソースとComputeパイプラインを初期化する
        bool Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager);

        /// @brief 1フレーム分の海面シミュレーションをDispatchし、出力テクスチャを更新する
        void Dispatch(ID3D12GraphicsCommandList* cmdList, float timeSeconds);

        /// @brief シミュレーション設定を更新し、必要に応じてスペクトルを再構築する
        void SetSettings(const Settings& settings);

        /// @brief 初期化済みかどうかを返す
        /// @return 初期化済みの場合 true
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 変位テクスチャのSRVハンドルを返す
        /// @return 変位SRVのGPUディスクリプタハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetDisplacementSRVHandle() const { return displacementSrvHandle_; }

        /// @brief 法線テクスチャのSRVハンドルを返す
        /// @return 法線SRVのGPUディスクリプタハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRVHandle() const { return normalSrvHandle_; }

        /// @brief ヤコビアンテクスチャのSRVハンドルを返す
        /// @return ヤコビアンSRVのGPUディスクリプタハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetJacobianSRVHandle() const { return jacobianSrvHandle_; }

        /// @brief 現在のシミュレーション設定を返す
        /// @return 設定参照
        const Settings& GetSettings() const { return settings_; }

        // マルチスケール・カスケード数。各カスケードは異なるパッチ長の独立したFFTで、
        // 周期が噛み合わないため単一タイルの「格子状の繰り返し」を打ち消す。
        // 出力は Texture2DArray（スライス = カスケード）にまとめ、水面シェーダ側で
        // ワールドXZベースに各スライスをサンプルして合算する。
        // 各カスケードのワールドパッチ長／波高配分は .cpp の kCascadePatchLength / kCascadeRmsShare、
        // パッチ長はシェーダ側 kFFTCascadePatch と一致させること。
        // 波高は Pierson-Moskowitz（Hs ≈ 0.21 v²/g）へ較正され、風速だけで決まる。
        static constexpr uint32_t kCascadeCount = 3;

    private:
        static constexpr uint32_t kMaxSpectrumComponents = 64;
        static constexpr uint32_t kPingPongCount = 2;
        // IFFT定数のリングスロット数。1フレームで全カスケードのIFFTパスを積むため、
        // （2 * log2(最大解像度512=9) * 2系統 = 36）× kCascadeCount 分を余裕を持って確保する。
        static constexpr uint32_t kMaxIFFTPassCount = 36 * kCascadeCount + 8;

        struct SpectrumSample {
            float h0[2] = {};
            float h0Minus[2] = {};
            float waveVector[2] = {};
            float angularFrequency = 0.0f;
            float directionalWeight = 0.0f;
            float padding[2] = {};
        };

        struct SimulationConstants {
            uint32_t resolution = 0;
            uint32_t activeComponentCount = 0;
            float patchLength = 1.0f;
            float timeSeconds = 0.0f;
            float choppiness = 1.0f;
            float gravity = 9.81f;
            float amplitudeScale = 1.0f;
            float padding = 0.0f;
        };

        struct IFFTConstants {
            uint32_t resolution = 0;
            uint32_t stageIndex = 0;
            uint32_t isHorizontal = 0;
            float normalizationScale = 1.0f;
            float padding[3] = {};
        };

        /// @brief 時間発展パス用Compute Shaderのパスを提供する
        struct TimeEvolutionShaderProvider final : ICustomShaderProvider {
            /// @brief 時間発展CSのファイルパスを返す
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanTimeEvolution.CS.hlsl"; }
        };

        /// @brief IFFTパス用Compute Shaderのパスを提供する
        struct IFFTShaderProvider final : ICustomShaderProvider {
            /// @brief IFFT CSのファイルパスを返す
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanIFFT.CS.hlsl"; }
        };

        /// @brief 最終合成パス用Compute Shaderのパスを提供する
        struct FinalizeShaderProvider final : ICustomShaderProvider {
            /// @brief 最終合成CSのファイルパスを返す
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanFinalize.CS.hlsl"; }
        };

        /// @brief 法線マップミップ生成パス用Compute Shaderのパスを提供する
        struct NormalMipGenShaderProvider final : ICustomShaderProvider {
            /// @brief 法線ミップ生成CSのファイルパスを返す
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanNormalMipGen.CS.hlsl"; }
        };

        /// @brief Computeパイプライン群を作成する
        bool CreatePipelines();

        /// @brief 最終出力テクスチャ(変位/法線/ヤコビアン)を作成する
        bool CreateOutputTextures();

        /// @brief デバッグ用Readbackバッファを作成する
        bool CreateDebugReadbackBuffers();

        /// @brief 初期スペクトル格納用バッファを作成する
        bool CreateSpectrumBuffer();

        /// @brief シミュレーション定数バッファを作成する
        bool CreateSimulationConstantBuffer();

        /// @brief IFFT用定数バッファを作成する
        bool CreateIFFTConstantBuffer();

        /// @brief IFFTピンポン用中間テクスチャを作成する
        bool CreateIntermediateTextures();

        /// @brief 設定値を有効範囲へ補正する
        void SanitizeSettings(Settings& settings) const;

        /// @brief 現在設定から初期スペクトルを再構築する
        void BuildSpectrum();

        /// @brief 指定カスケードのフレーム時刻・パッチ長を含むシミュレーション定数を更新する
        void UpdateSimulationConstants(uint32_t cascadeIndex, float timeSeconds);

        /// @brief 指定カスケードのシミュレーション定数スロットのGPU仮想アドレスを返す
        D3D12_GPU_VIRTUAL_ADDRESS GetSimulationConstantsAddress(uint32_t cascadeIndex) const;

        /// @brief IFFT 1パス分の定数を書き込み、GPU仮想アドレスを返す
        D3D12_GPU_VIRTUAL_ADDRESS UpdateIFFTConstants(uint32_t stageIndex, bool isHorizontal, float normalizationScale);

        /// @brief 指定カスケードの時間発展パスをDispatchする
        void DispatchEvolutionPass(ID3D12GraphicsCommandList* cmdList, uint32_t cascadeIndex);

        /// @brief IFFT 1ステージ分のパスをDispatchする
        void DispatchIFFTPass(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* inputResource,
            D3D12_RESOURCE_STATES& inputState,
            CustomShaderPipeline& pipeline,
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
            ID3D12Resource* outputResource,
            D3D12_RESOURCE_STATES& outputState,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUav,
            uint32_t stageIndex,
            bool isHorizontal,
            float normalizationScale);

        /// @brief 指定テクスチャ列に対して横方向/縦方向のIFFTを実行する
        uint32_t DispatchIFFTForTexture(
            ID3D12GraphicsCommandList* cmdList,
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount>& resources,
            std::array<D3D12_RESOURCE_STATES, kPingPongCount>& resourceStates,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& srvHandles,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& uavHandles,
            uint32_t initialIndex);

        /// @brief 法線マップのミップチェーンを生成する
        /// @details Finalize でミップ0へ書かれた法線を 2x2 平均で順次ダウンサンプルする。
        ///          呼び出し時点で normalTexture_ の全サブリソースが UNORDERED_ACCESS 状態である
        ///          前提で、退出時にも同状態へ戻す（呼び出し元の一括ステート管理を保つ）。
        void DispatchNormalMipGenPass(ID3D12GraphicsCommandList* cmdList);

        /// @brief IFFT結果から最終の変位/法線/ヤコビアンを生成する（指定カスケードのスライスへ書き込む）
        void DispatchFinalizePass(
            ID3D12GraphicsCommandList* cmdList,
            uint32_t cascadeIndex,
            ID3D12Resource* spectrumAResource,
            D3D12_RESOURCE_STATES& spectrumAState,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumASrv,
            ID3D12Resource* spectrumBResource,
            D3D12_RESOURCE_STATES& spectrumBState,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumBSrv);

        /// @brief IFFT Readback結果が揃っていればログ出力する
        void LogPendingIFFTDebugReadback();

        /// @brief IFFT結果のReadbackコピーをスケジュールする
        void ScheduleIFFTDebugReadback(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* spectrumAResource,
            D3D12_RESOURCE_STATES& spectrumAState,
            ID3D12Resource* spectrumBResource,
            D3D12_RESOURCE_STATES& spectrumBState);

        /// @brief 時間発展Readback結果が揃っていればログ出力する
        void LogPendingEvolutionDebugReadback();

        /// @brief 時間発展結果のReadbackコピーをスケジュールする
        void ScheduleEvolutionDebugReadback(ID3D12GraphicsCommandList* cmdList);

        /// @brief 最終出力Readback結果が揃っていればログ出力する
        void LogPendingDebugReadback();

        /// @brief 最終出力のReadbackコピーをスケジュールする
        void ScheduleDebugReadback(ID3D12GraphicsCommandList* cmdList);

        /// @brief 現在解像度に対するIFFTステージ数(log2)を返す
        /// @return IFFTステージ数
        uint32_t GetLog2Resolution() const;

        // ──────────────────────────────────────────────────────────
        // 基本依存とパイプライン状態
        // ──────────────────────────────────────────────────────────
        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        CustomShaderPipeline evolutionPipeline_{};
        CustomShaderPipeline ifftPipeline_{};
        CustomShaderPipeline finalizePipeline_{};
        CustomShaderPipeline normalMipGenPipeline_{};
        TimeEvolutionShaderProvider timeEvolutionShaderProvider_{};
        IFFTShaderProvider ifftShaderProvider_{};
        FinalizeShaderProvider finalizeShaderProvider_{};
        NormalMipGenShaderProvider normalMipGenShaderProvider_{};
        Settings settings_{};
        bool isInitialized_ = false;

        // ──────────────────────────────────────────────────────────
        // 最終出力テクスチャとReadbackリソース
        // ──────────────────────────────────────────────────────────
        Microsoft::WRL::ComPtr<ID3D12Resource> displacementTexture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> normalTexture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> jacobianTexture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> displacementReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> normalReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> evolutionAReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> evolutionBReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> ifftAReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> ifftBReadbackBuffer_;

        // ──────────────────────────────────────────────────────────
        // 出力テクスチャのSRV/UAVハンドルとリソース状態管理
        // ──────────────────────────────────────────────────────────
        // SRV は配列全体（全カスケード）を1ビューで見せる。UAV は Finalize が
        // カスケード単位で書き込むためスライスごとに用意する。
        D3D12_CPU_DESCRIPTOR_HANDLE displacementSrvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kCascadeCount> displacementUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kCascadeCount> displacementUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE normalSrvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kCascadeCount> normalUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kCascadeCount> normalUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE jacobianSrvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kCascadeCount> jacobianUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kCascadeCount> jacobianUavHandle_{};
        D3D12_RESOURCE_STATES displacementState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES normalState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES jacobianState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        // ──────────────────────────────────────────────────────────
        // 法線マップミップチェーン（かすめ角のフレネルスペックル対策）
        // ──────────────────────────────────────────────────────────
        uint32_t normalMipLevels_ = 1;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> normalMipSrvCpuHandles_;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> normalMipSrvHandles_;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> normalMipUavCpuHandles_;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> normalMipUavHandles_;

        // ──────────────────────────────────────────────────────────
        // Readbackレイアウト/サイズと非同期ログ制御
        // ──────────────────────────────────────────────────────────
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT displacementReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT normalReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT evolutionAReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT evolutionBReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT ifftAReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT ifftBReadbackLayout_{};
        UINT64 displacementReadbackBytes_ = 0;
        UINT64 normalReadbackBytes_ = 0;
        UINT64 evolutionAReadbackBytes_ = 0;
        UINT64 evolutionBReadbackBytes_ = 0;
        UINT64 ifftAReadbackBytes_ = 0;
        UINT64 ifftBReadbackBytes_ = 0;
        bool debugReadbackPending_ = false;
        bool evolutionDebugReadbackPending_ = false;
        bool ifftDebugReadbackPending_ = false;
        uint64_t debugReadbackSequence_ = 0;
        uint64_t evolutionDebugReadbackSequence_ = 0;
        uint64_t ifftDebugReadbackSequence_ = 0;

        // ──────────────────────────────────────────────────────────
        // IFFT用ピンポンテクスチャと対応ハンドル/状態
        // ──────────────────────────────────────────────────────────
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount> spectrumTextureA_;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount> spectrumTextureB_;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumASrvCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumASrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumAUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumAUavHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBSrvCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBSrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBUavHandle_{};
        std::array<D3D12_RESOURCE_STATES, kPingPongCount> spectrumAState_{};
        std::array<D3D12_RESOURCE_STATES, kPingPongCount> spectrumBState_{};

        // ──────────────────────────────────────────────────────────
        // 初期スペクトルバッファとアップロード状態
        // ──────────────────────────────────────────────────────────
        // 初期スペクトル（h0）はパッチ長依存のためカスケードごとに独立して持つ。
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kCascadeCount> spectrumBuffer_;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kCascadeCount> spectrumUploadBuffer_;
        std::array<SpectrumSample*, kCascadeCount> mappedSpectrumSamples_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kCascadeCount> spectrumSrvCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kCascadeCount> spectrumSrvHandle_{};
        std::array<D3D12_RESOURCE_STATES, kCascadeCount> spectrumBufferState_{};
        bool spectrumBufferDirty_ = false;

        // ──────────────────────────────────────────────────────────
        // シミュレーション/IFFT定数バッファと書き込みカーソル
        // ──────────────────────────────────────────────────────────
        // シミュレーション定数はカスケードごとに patchLength / amplitudeScale が異なるため、
        // 1フレームで全カスケードのDispatchを積むには kCascadeCount スロットのリングにする。
        Microsoft::WRL::ComPtr<ID3D12Resource> simulationConstantsBuffer_;
        uint8_t* mappedSimulationConstants_ = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> ifftConstantsBuffer_;
        uint8_t* mappedIFFTConstantsData_ = nullptr;
        uint32_t ifftConstantsWriteIndex_ = 0;
    };
}

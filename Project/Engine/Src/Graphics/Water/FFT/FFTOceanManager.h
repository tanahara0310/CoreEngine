#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RHI/Resource/PerFrameConstants.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/FFT/FFTOceanDebugProbe.h"
#include "Graphics/Water/FFT/FFTOceanGpuResources.h"
#include "Graphics/Water/FFT/FFTOceanSpectrum.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class GpuTimestampProfiler;

    /// @brief FFTベースの海面シミュレーションを実行し、変位/法線/ヤコビアンを生成する管理クラス
    /// @details 波のスペクトルの生成と統計は FFTOceanSpectrum が受け持つ
    class FFTOceanManager
    {
    public:
        /// @brief 海面スペクトルの設定
        using Settings = FFTOceanSpectrum::Settings;

        /// @brief カスケードの数（出力はスライス = カスケードの Texture2DArray）
        static constexpr uint32_t kCascadeCount = FFTOceanSpectrum::kCascadeCount;

        /// @brief 必要なGPUリソースとComputeパイプラインを初期化する
        /// @details スペクトルはまだ作らない。最初の SetSettings か Dispatch で作る。
        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator);

        /// @brief 1 フレーム分の海面シミュレーションを Dispatch し、出力テクスチャを更新する
        /// @param profiler 内訳計測用プロファイラ（nullptr なら計測しない）。
        ///                 パス単位計測では FFTOceanPass 1 本にまとまるので、ここで直接取る。
        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            float timeSeconds,
            GpuTimestampProfiler* profiler = nullptr);

        /// @brief シミュレーション設定を更新し、必要に応じてスペクトルを再構築する
        /// @details スペクトルをまだ作っていなければ、設定が同じでも作る。
        void SetSettings(const Settings& settings);

        /// @brief 初期化済みかどうかを返す
        /// @return 初期化済みの場合 true
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 変位テクスチャのSRVハンドルを返す
        /// @return 変位SRVのGPUディスクリプタハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetDisplacementSRVHandle() const { return displacementMap_.srv.gpuHandle; }

        /// @brief 法線テクスチャのSRVハンドルを返す
        /// @return 法線SRVのGPUディスクリプタハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRVHandle() const { return normalMap_.srv.gpuHandle; }

        /// @brief ヤコビアンテクスチャのSRVハンドルを返す
        /// @return ヤコビアンSRVのGPUディスクリプタハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetJacobianSRVHandle() const { return jacobianMap_.srv.gpuHandle; }

        /// @brief スペクトルの版（波面を作り直すたびに増える）
        uint32_t GetSpectrumRevision() const { return spectrumRevision_; }

        /// @brief 現在のシミュレーション設定を返す
        /// @return 設定参照
        const Settings& GetSettings() const { return spectrum_.GetSettings(); }

        /// @brief スペクトル（波の大きさ・波群の位相・平均二乗傾斜）
        const FFTOceanSpectrum& GetSpectrum() const { return spectrum_; }

    private:
        static constexpr uint32_t kPingPongCount = 2;

        // HLSL 側 FFTOceanTimeEvolution.CS.hlsl の SpectrumSample と一致必須
        using SpectrumSample = FFTOceanSpectrum::SpectrumSample;

        /// @brief シェーダー側 cbuffer と一致させる海面シミュレーションの定数
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

        /// @note HLSL 側 FFTOceanIFFT.CS.hlsl の cbuffer FFTOceanIFFTConstants は
        ///       この 4 メンバのみ（16B）。以前ここに float padding[3] が付いていて
        ///       C++ 側だけ 28B（16 の倍数ですらない）になっていたので削除した。
        struct IFFTConstants {
            uint32_t resolution = 0;
            uint32_t stageIndex = 0;
            uint32_t isHorizontal = 0;
            float normalizationScale = 1.0f;
        };

        // GPU へそのまま転送する構造体のレイアウト検証（HLSL 側との一致は目視だが、
        // C++ 側の不用意なフィールド追加・並び替えはここで検出する。
        // RTシャドウの cbuffer 配列ずれ事故と同型の予防）。
        static_assert(sizeof(SpectrumSample) == 40,
            "SpectrumSample must be 40 bytes (StructuredBuffer stride in FFTOceanTimeEvolution.CS.hlsl)");
        static_assert(sizeof(SimulationConstants) == 32,
            "SimulationConstants layout mismatch with FFTOceanSimulationConstants cbuffer");

        static constexpr Cb::Field kSpectrumSampleFields[] = {
            CB_FIELD(SpectrumSample, h0), CB_FIELD(SpectrumSample, h0Minus), CB_FIELD(SpectrumSample, waveVector),
            CB_FIELD(SpectrumSample, angularFrequency), CB_FIELD(SpectrumSample, directionalWeight),
            CB_FIELD(SpectrumSample, padding),
        };
        CB_VERIFY_STRIDE(SpectrumSample, kSpectrumSampleFields);

        static constexpr Cb::Field kSimulationConstantsFields[] = {
            CB_FIELD(SimulationConstants, resolution), CB_FIELD(SimulationConstants, activeComponentCount),
            CB_FIELD(SimulationConstants, patchLength), CB_FIELD(SimulationConstants, timeSeconds),
            CB_FIELD(SimulationConstants, choppiness), CB_FIELD(SimulationConstants, gravity),
            CB_FIELD(SimulationConstants, amplitudeScale), CB_FIELD(SimulationConstants, padding),
        };
        CB_VERIFY_LAYOUT(SimulationConstants, kSimulationConstantsFields);
        CB_BIND_HLSL(SimulationConstants, kSimulationConstantsFields, "FFTOceanSimulationConstants");

        static constexpr Cb::Field kIFFTConstantsFields[] = {
            CB_FIELD(IFFTConstants, resolution), CB_FIELD(IFFTConstants, stageIndex),
            CB_FIELD(IFFTConstants, isHorizontal), CB_FIELD(IFFTConstants, normalizationScale),
        };
        CB_VERIFY_LAYOUT(IFFTConstants, kIFFTConstantsFields);
        CB_BIND_HLSL(IFFTConstants, kIFFTConstantsFields, "FFTOceanIFFTConstants");

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

        /// @brief Computeパイプライン群を作成する
        bool CreatePipelines();

        /// @brief 最終出力テクスチャ(変位/法線/ヤコビアン)を作成する
        bool CreateOutputTextures();

        /// @brief 初期スペクトル格納用バッファを作成する
        bool CreateSpectrumBuffer();

        /// @brief IFFTピンポン用中間テクスチャを作成する
        bool CreateIntermediateTextures();

        /// @brief 現在設定から初期スペクトルを再構築し、UPLOAD 側へ書く
        void BuildSpectrum();

        /// @brief 指定カスケードのフレーム時刻・パッチ長を含むシミュレーション定数を作る
        void UpdateSimulationConstants(uint32_t cascadeIndex, float timeSeconds);

        /// @brief 指定カスケードのシミュレーション定数を記録中のフレームの UploadRing に置き、GPU仮想アドレスを返す
        D3D12_GPU_VIRTUAL_ADDRESS GetSimulationConstantsAddress(uint32_t cascadeIndex) const;

        /// @brief IFFT 1パス分の定数を記録中のフレームの UploadRing に置き、GPU仮想アドレスを返す
        D3D12_GPU_VIRTUAL_ADDRESS UploadIFFTConstants(uint32_t stageIndex, bool isHorizontal, float normalizationScale) const;

        /// @brief 指定カスケードの時間発展パスをDispatchする
        void DispatchEvolutionPass(ID3D12GraphicsCommandList* cmdList, uint32_t cascadeIndex);

        /// @brief 指定 ping-pong テクスチャ列に対して横方向/縦方向のIFFTを実行する
        /// @return 最終結果が入っている側の index
        uint32_t DispatchIFFTForTexture(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanPingPong& textures,
            uint32_t initialIndex);

        /// @brief IFFT結果から最終の変位/法線/ヤコビアンを生成する（指定カスケードのスライスへ書き込む）
        void DispatchFinalizePass(
            ID3D12GraphicsCommandList* cmdList,
            uint32_t cascadeIndex,
            FFTOceanGpuTexture& spectrumA,
            FFTOceanGpuTexture& spectrumB);

        /// @brief 現在解像度に対するIFFTステージ数(log2)を返す
        /// @return IFFTステージ数
        uint32_t GetLog2Resolution() const;

        // ──────────────────────────────────────────────────────────
        // 基本依存とパイプライン状態
        // ──────────────────────────────────────────────────────────
        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        CustomShaderPipeline evolutionPipeline_{};
        CustomShaderPipeline ifftPipeline_{};
        CustomShaderPipeline finalizePipeline_{};
        TimeEvolutionShaderProvider timeEvolutionShaderProvider_{};
        IFFTShaderProvider ifftShaderProvider_{};
        FinalizeShaderProvider finalizeShaderProvider_{};
        /// @brief スペクトルの設定・生成・統計
        FFTOceanSpectrum spectrum_{};
        /// @brief スペクトルの版（SetSettings で波面を作り直すたびに増やす）
        uint32_t spectrumRevision_ = 0;
        bool isInitialized_ = false;
        /// @brief スペクトルを作ったか（作るまでは GPU がスペクトルのバッファを読まない）
        bool spectrumBuilt_ = false;
        // 最後に Dispatch へ渡されたシミュレーション時刻。SetSettings がスペクトル
        // 再構築時に時刻を引き継ぐために使う（マップ済み UPLOAD からの読み戻し禁止）。
        float currentSimulationTime_ = 0.0f;

        /// @brief 最終出力テクスチャ（配列 SRV ＋ カスケード単位のスライス UAV）
        /// @details SRV は配列全体（全カスケード）を 1 ビューで見せる。UAV は Finalize が
        ///          カスケード単位で書き込むためスライスごとに用意する。
        struct CascadeOutputTexture : GpuResource {
            DescriptorHandle srv{};
            std::array<DescriptorHandle, kCascadeCount> sliceUav{};
        };

        // ──────────────────────────────────────────────────────────
        // 最終出力テクスチャ（変位 / 法線 / ヤコビアン）
        // ──────────────────────────────────────────────────────────
        CascadeOutputTexture displacementMap_{};
        CascadeOutputTexture normalMap_{};
        CascadeOutputTexture jacobianMap_{};

        // ──────────────────────────────────────────────────────────
        // デバッグ計測（CVar "d.FFTOcean.DebugProbe" でオプトイン。
        // リードバックバッファ・ログ間引きカウンタは probe が内部で持つ）
        // ──────────────────────────────────────────────────────────
        FFTOceanDebugProbe debugProbe_{};

        // ──────────────────────────────────────────────────────────
        // IFFT用ピンポンテクスチャ（A = 高さ+変位X / B = 変位Z）
        // ──────────────────────────────────────────────────────────
        FFTOceanPingPong spectrumPingPongA_{};
        FFTOceanPingPong spectrumPingPongB_{};

        // ──────────────────────────────────────────────────────────
        // 初期スペクトルバッファとアップロード状態
        // ──────────────────────────────────────────────────────────
        // 初期スペクトル（h0）はパッチ長依存のためカスケードごとに独立して持つ。
        std::array<FFTOceanSpectrumBufferSet, kCascadeCount> spectrumBuffers_{};
        bool spectrumBufferDirty_ = false;

        /// @brief カスケードの UPLOAD 側マップ先を要素型付きで返す
        SpectrumSample* MappedSpectrumSamples(uint32_t cascadeIndex) const {
            return static_cast<SpectrumSample*>(spectrumBuffers_[cascadeIndex].mapped);
        }

        // ──────────────────────────────────────────────────────────
        // シミュレーション定数（カスケードごと。毎フレーム UploadRing に置く）
        // ──────────────────────────────────────────────────────────
        std::array<PerFrameConstants<SimulationConstants>, kCascadeCount> simulationConstants_{};
    };
}

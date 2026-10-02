#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/FFTOceanGpuResources.h"
#include "Graphics/Water/WaterFoamDefaults.h"

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;

    /// @brief 泡の時間変化を GPU で進めるシステム
    /// @details 白波の泡は FFT の参照格子の上で、岸の泡はカメラの周りの範囲で水の粒の静止位置ごとに進める
    class WaterFoamSystem
    {
    public:
        /// @brief 岸の泡の格子の一辺のテクセル数
        static constexpr uint32_t kShoreResolution = 512;
        /// @brief 岸の泡の格子の 1 テクセルの幅 [m]
        static constexpr float kShoreTexelSize = 0.5f;
        /// @brief 寄せ・引きに使う、ならした海底の高さの一辺のテクセル数
        static constexpr uint32_t kSmoothSeabedResolution = 64;

        /// @brief 岸の泡を進めるのに使う、同じフレームの入力
        struct ShoreInput {
            D3D12_GPU_DESCRIPTOR_HANDLE seabedHeightSRV{}; ///< 海底の高さ（RTWaterSeabedPass の出力）
            uint32_t seabedResolution = 0;                 ///< 海底の高さの一辺のテクセル数
            float windowOriginXZ[2] = { 0.0f, 0.0f };      ///< 範囲の XZ の最小の角 [m]（海底の高さと同じ範囲）
            float windowSize = 0.0f;                       ///< 範囲の一辺 [m]
            D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV{}; ///< FFT の変位（カスケードの配列）
            float waterRestHeight = 0.0f;                  ///< 静水面の高さ [m]
            float timeSeconds = 0.0f;                      ///< FFT のシミュレーション時刻 [s]
            uint32_t spectrumRevision = 0;                 ///< FFT のスペクトルの版。変わったフレームは泡を捨てる
            uint64_t frameNumber = 0;                      ///< 同じフレームで 2 回進めないための番号
        };

        /// @brief 泡の設定（WaterRenderFeature が毎フレーム渡す）
        struct Settings {
            bool enabled = WaterFoamDefaults::kEnabled;
            /// @brief 海面から 10 m の高さの風速 [m/s]。白波の被覆率の目標を決める
            float windSpeed = 0.0f;
            /// @brief 白波の被覆率の目標に掛ける倍率（1.0 = Monahan の観測式）
            float whitecapScale = WaterFoamDefaults::kWhitecapScale;
            float cascadeWeights[3] = {
                WaterFoamDefaults::kCascadeWeights[0],
                WaterFoamDefaults::kCascadeWeights[1],
                WaterFoamDefaults::kCascadeWeights[2],
            };
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
        };

        /// @brief GPU 資源と計算パイプラインを作る
        /// @param fftResolution FFT の格子の一辺のテクセル数
        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator, uint32_t fftResolution);

        /// @brief 初期化済みか
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 設定を更新する（毎フレーム呼んでよい。無効から有効へ変わるか白波の倍率が変わると蓄積を捨てる）
        void SetSettings(const Settings& settings);

        /// @brief 白波の泡を 1 フレーム進める（FFT のヤコビアンが読み取り状態になった後に呼ぶ）
        /// @param jacobianSRV      FFT のヤコビアン（カスケードの配列）
        /// @param timeSeconds      FFT のシミュレーション時刻 [s]
        /// @param spectrumRevision FFT のスペクトルの版。変わったフレームは蓄積を捨てる
        void DispatchWhitecap(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
            float timeSeconds,
            uint32_t spectrumRevision);

        /// @brief 白波の泡の SRV（直近に書き終わった側。今フレームの書き込み先ではない方）
        D3D12_GPU_DESCRIPTOR_HANDLE GetWhitecapSRVHandle() const {
            return whitecap_[(whitecapFrameIndex_ + 1u) & 1u].srv.gpuHandle;
        }

        /// @brief 沖の白波の統計（合成ヤコビアンのヒストグラムと白波の被覆率の平均）を測る
        /// @details 同じフレームの枠で前に記録した測定（GPU の処理が済んだもの）を読み、
        ///          白波の被覆率の平均が目標になるようにしきい値を合わせ直す。
        ///          測定の記録は kStatisticsFrameInterval フレームに 1 回。DispatchWhitecap の後に呼ぶ
        /// @param frameIndex フレームの枠の番号（FrameSync::FrameIndex）
        void DispatchWhitecapStatistics(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
            uint32_t frameIndex);

        /// @brief 最後に読み戻した、沖の白波の被覆率の平均
        float GetMeasuredWhitecapCoverage() const { return measuredWhitecapCoverage_; }

        /// @brief 白波の被覆率の目標（Monahan の観測式 W = 3.84e-6·U^3.41 に倍率を掛けたもの）
        float GetTargetWhitecapCoverage() const;

        /// @brief 較正した砕けるしきい値（合成ヤコビアンがこれを下回ると砕ける）
        float GetWhitecapBias() const { return whitecapBias_; }

        /// @brief 較正した、しきい値からの立ち上がりの傾き
        float GetWhitecapGain() const { return whitecapGain_; }

        /// @brief 岸の泡を 1 フレーム進める（海底の高さを測った後・水面の合成の前に呼ぶ）
        void DispatchShore(ID3D12GraphicsCommandList* cmdList, const ShoreInput& input);

        /// @brief 岸の泡を進めているか（初期化済みで泡が有効）
        bool IsShoreActive() const { return isInitialized_ && settings_.enabled; }

        /// @brief 岸の泡の SRV（今フレームに書く側。(被覆率, 寄せ・引きのずれ x, z, 0)）
        D3D12_GPU_DESCRIPTOR_HANDLE GetShoreSRVHandle() const {
            return shore_[shoreFrameIndex_ & 1u].srv.gpuHandle;
        }

        /// @brief 岸の泡の、直近に書いた側の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetLatestShoreSRVHandle() const {
            return shore_[(shoreFrameIndex_ + 1u) & 1u].srv.gpuHandle;
        }

        /// @brief 岸の泡の、直近に書いた側のテクスチャ（ステート追跡つき）
        GpuResource& GetLatestShoreResource() { return shore_[(shoreFrameIndex_ + 1u) & 1u]; }

    private:
        static constexpr uint32_t kCascadeCount = 3;

        /// @brief 白波の蓄積パスの定数（FFTOceanFoamAccumulate.CS.hlsl の FFTOceanFoamConstants）
        struct WhitecapConstants {
            uint32_t resolution = 0;
            float deltaSeconds = 0.0f;
            float foamBias = WaterFoamDefaults::kBias;
            float foamGain = WaterFoamDefaults::kGain;
            float cascadeWeights[3] = {
                WaterFoamDefaults::kCascadeWeights[0],
                WaterFoamDefaults::kCascadeWeights[1],
                WaterFoamDefaults::kCascadeWeights[2],
            };
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
            uint32_t resetFoam = 0;
            float padding[3] = {};
        };

        static constexpr Cb::Field kWhitecapConstantsFields[] = {
            CB_FIELD(WhitecapConstants, resolution), CB_FIELD(WhitecapConstants, deltaSeconds),
            CB_FIELD(WhitecapConstants, foamBias), CB_FIELD(WhitecapConstants, foamGain),
            CB_FIELD(WhitecapConstants, cascadeWeights), CB_FIELD(WhitecapConstants, decaySeconds),
            CB_FIELD(WhitecapConstants, resetFoam), CB_FIELD(WhitecapConstants, padding),
        };
        CB_VERIFY_LAYOUT(WhitecapConstants, kWhitecapConstantsFields);
        CB_BIND_HLSL(WhitecapConstants, kWhitecapConstantsFields, "FFTOceanFoamConstants");

        /// @brief 岸の泡のパスの定数（WaterShoreFoam.CS.hlsl の WaterShoreFoamConstants）
        struct ShoreConstants {
            float windowOriginXZ[2] = { 0.0f, 0.0f };
            float prevWindowOriginXZ[2] = { 0.0f, 0.0f };
            float windowSize = 0.0f;
            float texelSize = kShoreTexelSize;
            uint32_t resolution = kShoreResolution;
            float waterRestHeight = 0.0f;
            float deltaSeconds = 0.0f;
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
            uint32_t resetFoam = 0;
            float padding = 0.0f;
        };

        static constexpr Cb::Field kShoreConstantsFields[] = {
            CB_FIELD(ShoreConstants, windowOriginXZ), CB_FIELD(ShoreConstants, prevWindowOriginXZ),
            CB_FIELD(ShoreConstants, windowSize), CB_FIELD(ShoreConstants, texelSize),
            CB_FIELD(ShoreConstants, resolution), CB_FIELD(ShoreConstants, waterRestHeight),
            CB_FIELD(ShoreConstants, deltaSeconds), CB_FIELD(ShoreConstants, decaySeconds),
            CB_FIELD(ShoreConstants, resetFoam), CB_FIELD(ShoreConstants, padding),
        };
        CB_VERIFY_LAYOUT(ShoreConstants, kShoreConstantsFields);
        CB_BIND_HLSL(ShoreConstants, kShoreConstantsFields, "WaterShoreFoamConstants");

        /// @brief 海底の高さをならすパスの定数（WaterShoreSeabedSmooth.CS.hlsl の WaterShoreSeabedSmoothConstants）
        struct SmoothSeabedConstants {
            uint32_t outputResolution = kSmoothSeabedResolution;
            uint32_t inputResolution = 0;
            float padding[2] = {};
        };

        static constexpr Cb::Field kSmoothSeabedConstantsFields[] = {
            CB_FIELD(SmoothSeabedConstants, outputResolution), CB_FIELD(SmoothSeabedConstants, inputResolution),
            CB_FIELD(SmoothSeabedConstants, padding),
        };
        CB_VERIFY_LAYOUT(SmoothSeabedConstants, kSmoothSeabedConstantsFields);
        CB_BIND_HLSL(SmoothSeabedConstants, kSmoothSeabedConstantsFields, "WaterShoreSeabedSmoothConstants");

        // ---- 沖の白波の統計 ----
        /// @brief 標本の格子の一辺の数
        static constexpr uint32_t kStatisticsSampleResolution = 256;
        /// @brief 標本の間隔 [m]
        static constexpr float kStatisticsSampleSpacing = 4.0f;
        /// @brief 合成ヤコビアンのヒストグラムの段の数と範囲
        static constexpr uint32_t kHistogramBins = 1024;
        static constexpr float kHistogramMin = -0.5f;
        static constexpr float kHistogramMax = 1.5f;
        /// @brief 統計のバッファの語数（段ごとの数 ＋ 被覆率の和）
        static constexpr uint32_t kStatisticsWords = kHistogramBins + 1;
        /// @brief 被覆率の和の整数の倍率（WaterWhitecapStatistics.CS.hlsl と同じ値）
        static constexpr float kCoverageFixedPointScale = 16384.0f;
        /// @brief 統計を測る間隔 [フレーム]
        static constexpr uint32_t kStatisticsFrameInterval = 4;
        /// @brief 測った被覆率をログへ出す間隔（読み戻しの回数）
        static constexpr uint32_t kStatisticsLogInterval = 150;

        // ---- 白波のしきい値の較正 ----
        /// @brief Monahan の観測式 W = kMonahanCoefficient·U^kMonahanExponent
        static constexpr float kMonahanCoefficient = 3.84e-6f;
        static constexpr float kMonahanExponent = 3.41f;
        /// @brief 白波が立ち始める風速 [m/s]（これ未満は白波を出さない）
        static constexpr float kMinWhitecapWindSpeed = 3.0f;
        /// @brief 目標の被覆率の範囲（下限未満は白波を出さない）
        static constexpr float kMinTargetCoverage = 1.0e-6f;
        static constexpr float kMaxTargetCoverage = 0.5f;
        /// @brief 砕ける点の割合 ÷ 目標の被覆率 の初期値と範囲
        static constexpr float kInitialBreakingRatio = 1.5f;
        static constexpr float kMinBreakingRatio = 0.25f;
        static constexpr float kMaxBreakingRatio = 8.0f;
        /// @brief 砕ける点の割合の上限
        static constexpr float kMaxBreakingFraction = 0.6f;
        /// @brief 被覆率が 1 になる点の、砕ける点に対する割合
        static constexpr float kFullCoverageShare = 0.25f;
        /// @brief 測った被覆率を目標へ寄せる速さ [1/s]（被覆率の log の差に掛ける）
        static constexpr float kCalibrationRate = 0.3f;
        /// @brief 被覆率の log の差の上限（1 回の読み戻しで動かす量を抑える）
        static constexpr float kMaxCalibrationError = 2.0f;
        /// @brief しきい値をならす時定数 [s]
        static constexpr float kThresholdSmoothingSeconds = 0.5f;
        /// @brief しきい値から被覆率 1 までの detJ の幅の下限
        static constexpr float kMinRampWidth = 5.0e-4f;
        /// @brief 白波を出さないときのしきい値
        static constexpr float kNoWhitecapBias = -10.0f;

        /// @brief 白波の統計のパスの定数（WaterWhitecapStatistics.CS.hlsl の WaterWhitecapStatisticsConstants）
        struct StatisticsConstants {
            float sampleOriginXZ[2] = { 0.0f, 0.0f };
            float sampleSpacing = kStatisticsSampleSpacing;
            uint32_t sampleResolution = kStatisticsSampleResolution;
            float cascadeWeights[3] = {
                WaterFoamDefaults::kCascadeWeights[0],
                WaterFoamDefaults::kCascadeWeights[1],
                WaterFoamDefaults::kCascadeWeights[2],
            };
            float bias = WaterFoamDefaults::kBias;
            float gain = WaterFoamDefaults::kGain;
            float histogramMin = kHistogramMin;
            float histogramInvWidth = 0.0f;
            uint32_t histogramBins = kHistogramBins;
        };

        static constexpr Cb::Field kStatisticsConstantsFields[] = {
            CB_FIELD(StatisticsConstants, sampleOriginXZ), CB_FIELD(StatisticsConstants, sampleSpacing),
            CB_FIELD(StatisticsConstants, sampleResolution), CB_FIELD(StatisticsConstants, cascadeWeights),
            CB_FIELD(StatisticsConstants, bias), CB_FIELD(StatisticsConstants, gain),
            CB_FIELD(StatisticsConstants, histogramMin), CB_FIELD(StatisticsConstants, histogramInvWidth),
            CB_FIELD(StatisticsConstants, histogramBins),
        };
        CB_VERIFY_LAYOUT(StatisticsConstants, kStatisticsConstantsFields);
        CB_BIND_HLSL(StatisticsConstants, kStatisticsConstantsFields, "WaterWhitecapStatisticsConstants");

        /// @brief 白波の統計のパスの計算シェーダー
        struct StatisticsShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterWhitecapStatistics.CS.hlsl"; }
        };

        /// @brief 白波の蓄積パスの計算シェーダー
        struct WhitecapShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanFoamAccumulate.CS.hlsl"; }
        };

        /// @brief 寄せ・引きのずれのパスの定数（WaterShoreSwash.CS.hlsl の WaterShoreSwashConstants）
        struct SwashConstants {
            float windowOriginXZ[2] = { 0.0f, 0.0f };
            float windowSize = 0.0f;
            uint32_t resolution = kSmoothSeabedResolution;
            float waterRestHeight = 0.0f;
            float padding[3] = {};
        };

        static constexpr Cb::Field kSwashConstantsFields[] = {
            CB_FIELD(SwashConstants, windowOriginXZ), CB_FIELD(SwashConstants, windowSize),
            CB_FIELD(SwashConstants, resolution), CB_FIELD(SwashConstants, waterRestHeight),
            CB_FIELD(SwashConstants, padding),
        };
        CB_VERIFY_LAYOUT(SwashConstants, kSwashConstantsFields);
        CB_BIND_HLSL(SwashConstants, kSwashConstantsFields, "WaterShoreSwashConstants");

        /// @brief 寄せ・引きのずれのパスの計算シェーダー
        struct SwashShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreSwash.CS.hlsl"; }
        };

        /// @brief 岸の泡のパスの計算シェーダー
        struct ShoreShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreFoam.CS.hlsl"; }
        };

        /// @brief 海底の高さをならすパスの計算シェーダー
        struct SmoothSeabedShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreSeabedSmooth.CS.hlsl"; }
        };

        bool CreatePipelines();
        bool CreateWhitecapResources();
        bool CreateShoreResources();
        bool CreateStatisticsResources();
        /// @brief 白波の蓄積と較正をやり直す（波面か白波の目標が変わったとき）
        void RequestWhitecapReset();
        /// @brief フレームの枠に記録してあった白波の統計を読む
        /// @return やり直した後に記録した統計を読めたか
        bool ReadWhitecapStatistics(uint32_t frameIndex);
        /// @brief 読み戻した統計から、白波の被覆率の平均が目標になるしきい値を求め直す
        void UpdateWhitecapCalibration();
        /// @brief 合成ヤコビアンのヒストグラムで、小さい方から数えた割合が fraction になる detJ
        float FindHistogramQuantile(float fraction) const;

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        bool isInitialized_ = false;
        Settings settings_{};

        // ---- 白波（FFT の参照格子の上の蓄積）----
        CustomShaderPipeline whitecapPipeline_{};
        WhitecapShaderProvider whitecapShaderProvider_{};
        uint32_t fftResolution_ = 0;
        /// @brief 書き込み先は whitecapFrameIndex_ & 1。SRV / UAV とも全カスケードを 1 ビューで見せる
        FFTOceanPingPong whitecap_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> whitecapConstantsBuffer_;
        WhitecapConstants* mappedWhitecapConstants_ = nullptr;
        uint32_t whitecapFrameIndex_ = 0;
        bool whitecapResetPending_ = true;
        float whitecapPreviousTimeSeconds_ = 0.0f;
        uint32_t whitecapSpectrumRevision_ = 0;

        // ---- 沖の白波の統計（フレームの枠ごとに読み戻す）----
        CustomShaderPipeline statisticsPipeline_{};
        StatisticsShaderProvider statisticsShaderProvider_{};
        GpuResource statistics_{};
        DescriptorHandle statisticsUav_{};
        /// @brief 統計のバッファを 0 にするための写し元（作ったまま書かない）
        Microsoft::WRL::ComPtr<ID3D12Resource> statisticsZero_;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> statisticsReadback_{};
        std::array<bool, kMaxFramesInFlight> statisticsPending_{};
        /// @brief 枠ごとの、記録したときの calibrationEpoch_
        std::array<uint32_t, kMaxFramesInFlight> statisticsEpoch_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> statisticsConstantsBuffer_;
        StatisticsConstants* mappedStatisticsConstants_ = nullptr;
        /// @brief 最後に読み戻した合成ヤコビアンのヒストグラム
        std::vector<uint32_t> whitecapHistogram_;
        float measuredWhitecapCoverage_ = 0.0f;
        uint32_t statisticsFrameCounter_ = 0;
        uint32_t statisticsLogCounter_ = 0;

        // ---- 白波のしきい値の較正 ----
        float whitecapBias_ = WaterFoamDefaults::kBias;
        float whitecapGain_ = WaterFoamDefaults::kGain;
        /// @brief 砕ける点の割合 ÷ 目標の被覆率 の log（測った被覆率から学ぶ）
        float logBreakingRatio_ = 0.0f;
        /// @brief やり直した後に一度しきい値を求めたか（最初の 1 回はならさずに合わせる）
        bool whitecapCalibrated_ = false;
        /// @brief 白波の蓄積と較正をやり直した回数（やり直す前に記録した統計は使わない）
        uint32_t calibrationEpoch_ = 0;
        /// @brief 前にしきい値を求めてから白波が進んだシミュレーション時間 [s]
        float whitecapElapsedSeconds_ = 0.0f;

        // ---- 岸の泡（カメラの周りの範囲。水の粒の静止位置ごと）----
        CustomShaderPipeline shorePipeline_{};
        ShoreShaderProvider shoreShaderProvider_{};
        /// @brief 書き込み先は shoreFrameIndex_ & 1
        FFTOceanPingPong shore_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> shoreConstantsBuffer_;
        ShoreConstants* mappedShoreConstants_ = nullptr;
        uint32_t shoreFrameIndex_ = 0;
        bool shoreResetPending_ = true;
        float shorePreviousTimeSeconds_ = 0.0f;
        uint32_t shoreSpectrumRevision_ = 0;
        float shorePrevWindowOriginXZ_[2] = { 0.0f, 0.0f };
        uint64_t shoreLastFrameNumber_ = UINT64_MAX;

        // ---- ならした海底の高さ（寄せ・引きの勾配と水深を測る）----
        CustomShaderPipeline smoothSeabedPipeline_{};
        SmoothSeabedShaderProvider smoothSeabedShaderProvider_{};
        FFTOceanGpuTexture smoothSeabed_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> smoothSeabedConstantsBuffer_;
        SmoothSeabedConstants* mappedSmoothSeabedConstants_ = nullptr;

        // ---- 寄せ・引きのずれ（ならした海底と同じ粗い格子）----
        CustomShaderPipeline swashPipeline_{};
        SwashShaderProvider swashShaderProvider_{};
        FFTOceanGpuTexture swash_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> swashConstantsBuffer_;
        SwashConstants* mappedSwashConstants_ = nullptr;
    };
}

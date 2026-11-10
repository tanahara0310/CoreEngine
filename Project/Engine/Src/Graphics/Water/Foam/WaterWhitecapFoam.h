#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/FFT/FFTOceanGpuResources.h"
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

    /// @brief 沖の白波の泡を GPU で進める（カメラの周りの格子の上の残りの泡と、被覆率に合わせたしきい値の較正）
    class WaterWhitecapFoam
    {
    public:
        /// @brief 白波の設定（WaterSystem が毎フレーム渡す）
        struct Settings {
            bool enabled = WaterFoamDefaults::kEnabled;
            /// @brief 海面から 10 m の高さの風速 [m/s]。白波の被覆率の目標を決める
            float windSpeed = 0.0f;
            /// @brief 白波の被覆率の目標に掛ける倍率（1.0 = Monahan の観測式）
            float whitecapScale = WaterFoamDefaults::kWhitecapScale;
            /// @brief 風下の向き（ワールド XZ・正規化済み）
            float windDirection[2] = { 1.0f, 0.0f };
            float cascadeWeights[3] = {
                WaterFoamDefaults::kCascadeWeights[0],
                WaterFoamDefaults::kCascadeWeights[1],
                WaterFoamDefaults::kCascadeWeights[2],
            };
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
        };

        /// @brief 残りの泡の格子の段の数（0 = カメラの近く・1 = 遠く）
        static constexpr uint32_t kLevelCount = 2;
        /// @brief 段の一辺のテクセル数
        static constexpr uint32_t kResolution = 1024;
        /// @brief 段ごとの 1 テクセルの幅 [m]
        static constexpr std::array<float, kLevelCount> kLevelTexelSizes = { 0.25f, 1.0f };

        /// @brief GPU 資源と計算パイプラインを作る
        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator);

        /// @brief 初期化済みか
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 設定を更新する（毎フレーム呼んでよい。無効から有効へ変わるか白波の倍率が変わると蓄積を捨てる）
        void SetSettings(const Settings& settings);

        /// @brief 残りの泡の格子をカメラの周りへ置き直す（段ごとに角をテクセルの格子に揃える）
        /// @details 動かすのは次の Dispatch で進める段だけ。ほかの段は中身と一緒に今の位置に残す
        /// @param cameraX     カメラのワールド X [m]
        /// @param cameraZ     カメラのワールド Z [m]
        /// @param timeSeconds FFT のシミュレーション時刻 [s]（泡が風下へ流れた距離を求める）
        void UpdateWindows(float cameraX, float cameraZ, float timeSeconds);

        /// @brief 白波の泡を 1 フレーム進める（FFT のヤコビアンが読み取り状態になった後に呼ぶ）
        /// @details 段を 1 フレームに 1 つずつ順に進め、ほかの段は前のフレームの値を写す
        /// @param jacobianSRV      FFT のヤコビアン（カスケードの配列）
        /// @param timeSeconds      FFT のシミュレーション時刻 [s]
        /// @param spectrumRevision FFT のスペクトルの版。変わったフレームは蓄積を捨てる
        /// @param waveGroupPhase   波群エンベロープの位相のずれ [rad]（FFTOceanSpectrum::ComputeWaveGroupPhase）
        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
            float timeSeconds,
            uint32_t spectrumRevision,
            const std::array<float, 3>& waveGroupPhase);

        /// @brief 白波の泡の SRV（直近に書き終わった側。今フレームの書き込み先ではない方）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const {
            return foam_[(frameIndex_ + 1u) & 1u].srv.gpuHandle;
        }

        /// @brief 段ごとの XZ の最小の角 [m]（近く xy・遠く zw。風下へ流した座標）
        const std::array<float, 4>& GetResidueOrigins() const { return levelOrigins_; }

        /// @brief 段ごとの一辺の長さの逆数 [1/m]
        std::array<float, kLevelCount> GetResidueInvSizes() const;

        /// @brief 最後に読み戻した、残りの泡の被覆率の平均（遠くの段より外で使う）
        float GetMeasuredResidueCoverage() const { return measuredResidueCoverage_; }

        /// @brief 最後に読み戻した、砕けている最中の白波の被覆率の平均
        float GetMeasuredInstantCoverage() const { return measuredInstantCoverage_; }

        /// @brief 沖の白波の統計（合成ヤコビアンのヒストグラムと白波の被覆率の平均）を測る
        /// @details 同じフレームの枠で前に記録した測定（GPU の処理が済んだもの）を読み、
        ///          白波の被覆率の平均が目標になるようにしきい値を合わせ直す。
        ///          測定の記録は kStatisticsFrameInterval フレームに 1 回。Dispatch の後に呼ぶ
        /// @param frameIndex     フレームの枠の番号（FrameSync::FrameIndex）
        /// @param waveGroupPhase 波群エンベロープの位相のずれ [rad]（FFTOceanSpectrum::ComputeWaveGroupPhase）
        void DispatchStatistics(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
            uint32_t frameIndex,
            const std::array<float, 3>& waveGroupPhase);

        /// @brief 最後に読み戻した、沖の白波の被覆率の平均
        float GetMeasuredCoverage() const { return measuredCoverage_; }

        /// @brief 白波の被覆率の目標（Monahan の観測式 W = 3.84e-6·U^3.41 に倍率を掛けたもの）
        float GetTargetCoverage() const;

        /// @brief 白波の泡が風下へ流れた距離（ワールド XZ）[m]
        /// @param timeSeconds FFT のシミュレーション時刻 [s]
        std::array<float, 2> ComputeDriftOffset(float timeSeconds) const;

        /// @brief 白波の泡の模様を風の向きに伸ばす軸（風下の向き × √(1 − 1/伸び率)）
        /// @details 模様の座標 p を p − a·(a·p) にすると、風の向きの成分だけが 1/伸び率 倍になる
        std::array<float, 2> ComputeStretchAxis() const;

        /// @brief 泡の模様を動かす時刻 [s]（FFT の時刻を kPatternPeriodSeconds で巻き戻したもの）
        /// @param timeSeconds FFT のシミュレーション時刻 [s]
        static float ComputePatternTime(float timeSeconds);

        /// @brief 較正した砕けるしきい値（合成ヤコビアンがこれを下回ると砕ける）
        float GetBias() const { return bias_; }

        /// @brief 較正した、しきい値からの立ち上がりの傾き
        float GetGain() const { return gain_; }

    private:
        /// @brief 泡が風下へ流れる速さ ÷ 風速（海面の吹送流とストークスドリフトの和）
        static constexpr float kWindDriftFactor = 0.03f;
        /// @brief 模様を風の向きに伸ばし始める風速と、伸び率が最大になる風速 [m/s]
        static constexpr float kStretchStartWindSpeed = 8.0f;
        static constexpr float kStretchFullWindSpeed = 18.0f;
        /// @brief 模様の伸び率の最大
        static constexpr float kMaxFoamStretch = 3.0f;
        /// @brief 泡の模様を動かす時刻の周期 [s]（WaterFoamAppearance.hlsli の kFoamPatternPeriodSeconds と同じ値）
        static constexpr float kPatternPeriodSeconds = 600.0f;

        /// @brief 白波の蓄積パスの定数（FFTOceanFoamAccumulate.CS.hlsl の FFTOceanFoamConstants）
        struct FoamConstants {
            float levelOriginsXZ[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float prevLevelOriginsXZ[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float levelTexelSizes[2] = { kLevelTexelSizes[0], kLevelTexelSizes[1] };
            uint32_t resolution = kResolution;
            float deltaSeconds = 0.0f;
            float cascadeWeights[3] = {
                WaterFoamDefaults::kCascadeWeights[0],
                WaterFoamDefaults::kCascadeWeights[1],
                WaterFoamDefaults::kCascadeWeights[2],
            };
            float foamBias = WaterFoamDefaults::kBias;
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f };
            float foamGain = WaterFoamDefaults::kGain;
            float driftOffsetXZ[2] = { 0.0f, 0.0f };
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
            uint32_t resetFoam = 0;
            uint32_t activeLevel = 0;
            float padding[3] = { 0.0f, 0.0f, 0.0f };
        };

        static constexpr Cb::Field kFoamConstantsFields[] = {
            CB_FIELD(FoamConstants, levelOriginsXZ), CB_FIELD(FoamConstants, prevLevelOriginsXZ),
            CB_FIELD(FoamConstants, levelTexelSizes), CB_FIELD(FoamConstants, resolution),
            CB_FIELD(FoamConstants, deltaSeconds), CB_FIELD(FoamConstants, cascadeWeights),
            CB_FIELD(FoamConstants, foamBias), CB_FIELD(FoamConstants, waveGroupPhase),
            CB_FIELD(FoamConstants, foamGain), CB_FIELD(FoamConstants, driftOffsetXZ),
            CB_FIELD(FoamConstants, decaySeconds), CB_FIELD(FoamConstants, resetFoam),
            CB_FIELD(FoamConstants, activeLevel), CB_FIELD(FoamConstants, padding),
        };
        CB_VERIFY_LAYOUT(FoamConstants, kFoamConstantsFields);
        CB_BIND_HLSL(FoamConstants, kFoamConstantsFields, "FFTOceanFoamConstants");

        // ---- 沖の白波の統計 ----
        /// @brief 標本の格子の一辺の数
        static constexpr uint32_t kStatisticsSampleResolution = 256;
        /// @brief 標本の間隔 [m]
        static constexpr float kStatisticsSampleSpacing = 4.0f;
        /// @brief 合成ヤコビアンのヒストグラムの段の数と範囲
        static constexpr uint32_t kHistogramBins = 1024;
        static constexpr float kHistogramMin = -0.5f;
        static constexpr float kHistogramMax = 1.5f;
        /// @brief 統計のバッファの語数（段ごとの数 ＋ 白波・砕けている最中・残りの泡の被覆率の和）
        static constexpr uint32_t kStatisticsWords = kHistogramBins + 3;
        /// @brief 被覆率の和の整数の倍率（WaterWhitecapStatistics.CS.hlsl と同じ値）
        static constexpr float kCoverageFixedPointScale = 16384.0f;
        /// @brief 統計を測る間隔 [フレーム]
        static constexpr uint32_t kStatisticsFrameInterval = 4;
        /// @brief 測った被覆率をログへ出す間隔（読み戻しの回数）
        static constexpr uint32_t kStatisticsLogInterval = 150;

        // ---- しきい値の較正 ----
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
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f };
            float residueMeanCoverage = 0.0f;
            float residueOriginsXZ[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float residueInvSizes[2] = { 0.0f, 0.0f };
            float driftOffsetXZ[2] = { 0.0f, 0.0f };
        };

        static constexpr Cb::Field kStatisticsConstantsFields[] = {
            CB_FIELD(StatisticsConstants, sampleOriginXZ), CB_FIELD(StatisticsConstants, sampleSpacing),
            CB_FIELD(StatisticsConstants, sampleResolution), CB_FIELD(StatisticsConstants, cascadeWeights),
            CB_FIELD(StatisticsConstants, bias), CB_FIELD(StatisticsConstants, gain),
            CB_FIELD(StatisticsConstants, histogramMin), CB_FIELD(StatisticsConstants, histogramInvWidth),
            CB_FIELD(StatisticsConstants, histogramBins), CB_FIELD(StatisticsConstants, waveGroupPhase),
            CB_FIELD(StatisticsConstants, residueMeanCoverage), CB_FIELD(StatisticsConstants, residueOriginsXZ),
            CB_FIELD(StatisticsConstants, residueInvSizes), CB_FIELD(StatisticsConstants, driftOffsetXZ),
        };
        CB_VERIFY_LAYOUT(StatisticsConstants, kStatisticsConstantsFields);
        CB_BIND_HLSL(StatisticsConstants, kStatisticsConstantsFields, "WaterWhitecapStatisticsConstants");

        /// @brief 白波の蓄積パスの計算シェーダー
        struct FoamShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanFoamAccumulate.CS.hlsl"; }
        };

        /// @brief 白波の統計のパスの計算シェーダー
        struct StatisticsShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterWhitecapStatistics.CS.hlsl"; }
        };

        bool CreatePipelines();
        bool CreateFoamResources();
        bool CreateStatisticsResources();
        /// @brief 蓄積と較正をやり直す（波面か白波の目標が変わったとき）
        void RequestReset();
        /// @brief フレームの枠に記録してあった統計を読む
        /// @return やり直した後に記録した統計を読めたか
        bool ReadStatistics(uint32_t frameIndex);
        /// @brief 読み戻した統計から、白波の被覆率の平均が目標になるしきい値を求め直す
        void UpdateCalibration();
        /// @brief 合成ヤコビアンのヒストグラムで、小さい方から数えた割合が fraction になる detJ
        float FindHistogramQuantile(float fraction) const;

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        bool isInitialized_ = false;
        Settings settings_{};

        // ---- 白波（カメラの周りの格子の上の残りの泡）----
        CustomShaderPipeline foamPipeline_{};
        FoamShaderProvider foamShaderProvider_{};
        /// @brief 書き込み先は frameIndex_ & 1。SRV / UAV とも全段を 1 ビューで見せる（x = 被覆率・y = 砕けた直後の厚い泡の量）
        FFTOceanPingPong foam_{};
        uint32_t frameIndex_ = 0;
        bool resetPending_ = true;
        float previousTimeSeconds_ = 0.0f;
        uint32_t spectrumRevision_ = 0;
        /// @brief このフレームで白波の泡が風下へ流れた距離 [m]（Dispatch が求める）
        std::array<float, 2> driftOffset_{};
        /// @brief 段ごとの XZ の最小の角 [m]（近く xy・遠く zw。UpdateWindows が進める段だけ動かす）
        std::array<float, 4> levelOrigins_{};
        /// @brief 前に Dispatch で書いたときの段ごとの角 [m]
        std::array<float, 4> prevLevelOrigins_{};
        /// @brief 段ごとの、最後に進めてから経ったシミュレーション時間 [s]
        std::array<float, kLevelCount> pendingSeconds_{};

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
        /// @brief 最後に読み戻した合成ヤコビアンのヒストグラム
        std::vector<uint32_t> histogram_;
        float measuredCoverage_ = 0.0f;
        float measuredInstantCoverage_ = 0.0f;
        float measuredResidueCoverage_ = 0.0f;
        uint32_t statisticsFrameCounter_ = 0;
        uint32_t statisticsLogCounter_ = 0;

        // ---- しきい値の較正 ----
        float bias_ = WaterFoamDefaults::kBias;
        float gain_ = WaterFoamDefaults::kGain;
        /// @brief 砕ける点の割合 ÷ 目標の被覆率 の log（測った被覆率から学ぶ）
        float logBreakingRatio_ = 0.0f;
        /// @brief やり直した後に一度しきい値を求めたか（最初の 1 回はならさずに合わせる）
        bool calibrated_ = false;
        /// @brief 蓄積と較正をやり直した回数（やり直す前に記録した統計は使わない）
        uint32_t calibrationEpoch_ = 0;
        /// @brief 前にしきい値を求めてから白波が進んだシミュレーション時間 [s]
        float elapsedSeconds_ = 0.0f;
    };
}

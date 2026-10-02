#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/FFTOceanGpuResources.h"
#include "Graphics/Water/WaterFoamDefaults.h"

#include <cstdint>
#include <d3d12.h>
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

        /// @brief 泡の設定（WaterRenderFeature が WaterFrameConstants の値を毎フレーム渡す）
        struct Settings {
            bool enabled = WaterFoamDefaults::kEnabled;
            float bias = WaterFoamDefaults::kBias;
            float gain = WaterFoamDefaults::kGain;
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

        /// @brief 設定を更新する（毎フレーム呼んでよい。無効から有効へ変わると蓄積を捨てる）
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

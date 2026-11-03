#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/FFT/FFTOceanGpuResources.h"
#include "Graphics/Water/WaterFoamDefaults.h"

#include <cstdint>
#include <d3d12.h>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;

    /// @brief 岸の泡を GPU で進める（カメラの周りの範囲で、水の粒の静止位置ごとの泡と寄せ・引きのずれ）
    class WaterShoreFoam
    {
    public:
        /// @brief 岸の泡の格子の一辺のテクセル数
        static constexpr uint32_t kResolution = 512;
        /// @brief 岸の泡の格子の 1 テクセルの幅 [m]
        static constexpr float kTexelSize = 0.5f;
        /// @brief 寄せ・引きに使う、ならした海底の高さの一辺のテクセル数
        static constexpr uint32_t kSmoothSeabedResolution = 64;

        /// @brief 岸の泡を進めるのに使う、同じフレームの入力
        struct Input {
            D3D12_GPU_DESCRIPTOR_HANDLE seabedHeightSRV{}; ///< 海底の高さ（RTWaterSeabedPass の出力）
            uint32_t seabedResolution = 0;                 ///< 海底の高さの一辺のテクセル数
            float windowOriginXZ[2] = { 0.0f, 0.0f };      ///< 範囲の XZ の最小の角 [m]（海底の高さと同じ範囲）
            float windowSize = 0.0f;                       ///< 範囲の一辺 [m]
            D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV{}; ///< FFT の変位（カスケードの配列）
            float waterRestHeight = 0.0f;                  ///< 静水面の高さ [m]
            float timeSeconds = 0.0f;                      ///< FFT のシミュレーション時刻 [s]
            uint32_t spectrumRevision = 0;                 ///< FFT のスペクトルの版。変わったフレームは泡を捨てる
            uint64_t frameNumber = 0;                      ///< 同じフレームで 2 回進めないための番号
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f }; ///< 波群エンベロープの位相のずれ [rad]
        };

        /// @brief 岸の泡の設定（WaterSystem が毎フレーム渡す）
        struct Settings {
            bool enabled = WaterFoamDefaults::kEnabled;
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
        };

        /// @brief GPU 資源と計算パイプラインを作る
        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator);

        /// @brief 初期化済みか
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 設定を更新する（毎フレーム呼んでよい。無効から有効へ変わると泡を捨てる）
        void SetSettings(const Settings& settings);

        /// @brief 岸の泡を 1 フレーム進める（海底の高さを測った後・水面の合成の前に呼ぶ）
        void Dispatch(ID3D12GraphicsCommandList* cmdList, const Input& input);

        /// @brief 岸の泡を進めているか（初期化済みで泡が有効）
        bool IsActive() const { return isInitialized_ && settings_.enabled; }

        /// @brief 岸の泡の SRV（今フレームに書く側。(被覆率, 寄せ・引きのずれ x, z, 0)）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const {
            return foam_[frameIndex_ & 1u].srv.gpuHandle;
        }

        /// @brief 岸の泡の、直近に書いた側の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetLatestSRVHandle() const {
            return foam_[(frameIndex_ + 1u) & 1u].srv.gpuHandle;
        }

        /// @brief 岸の泡の、直近に書いた側のテクスチャ（ステート追跡つき）
        GpuResource& GetLatestResource() { return foam_[(frameIndex_ + 1u) & 1u]; }

    private:
        /// @brief 岸の泡のパスの定数（WaterShoreFoam.CS.hlsl の WaterShoreFoamConstants）
        struct FoamConstants {
            float windowOriginXZ[2] = { 0.0f, 0.0f };
            float prevWindowOriginXZ[2] = { 0.0f, 0.0f };
            float windowSize = 0.0f;
            float texelSize = kTexelSize;
            uint32_t resolution = kResolution;
            float waterRestHeight = 0.0f;
            float deltaSeconds = 0.0f;
            float decaySeconds = WaterFoamDefaults::kDecaySeconds;
            uint32_t resetFoam = 0;
            float padding = 0.0f;
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f };
            float padding2 = 0.0f;
        };

        static constexpr Cb::Field kFoamConstantsFields[] = {
            CB_FIELD(FoamConstants, windowOriginXZ), CB_FIELD(FoamConstants, prevWindowOriginXZ),
            CB_FIELD(FoamConstants, windowSize), CB_FIELD(FoamConstants, texelSize),
            CB_FIELD(FoamConstants, resolution), CB_FIELD(FoamConstants, waterRestHeight),
            CB_FIELD(FoamConstants, deltaSeconds), CB_FIELD(FoamConstants, decaySeconds),
            CB_FIELD(FoamConstants, resetFoam), CB_FIELD(FoamConstants, padding),
            CB_FIELD(FoamConstants, waveGroupPhase), CB_FIELD(FoamConstants, padding2),
        };
        CB_VERIFY_LAYOUT(FoamConstants, kFoamConstantsFields);
        CB_BIND_HLSL(FoamConstants, kFoamConstantsFields, "WaterShoreFoamConstants");

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

        /// @brief 寄せ・引きのずれのパスの定数（WaterShoreSwash.CS.hlsl の WaterShoreSwashConstants）
        struct SwashConstants {
            float windowOriginXZ[2] = { 0.0f, 0.0f };
            float windowSize = 0.0f;
            uint32_t resolution = kSmoothSeabedResolution;
            float waterRestHeight = 0.0f;
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f };
        };

        static constexpr Cb::Field kSwashConstantsFields[] = {
            CB_FIELD(SwashConstants, windowOriginXZ), CB_FIELD(SwashConstants, windowSize),
            CB_FIELD(SwashConstants, resolution), CB_FIELD(SwashConstants, waterRestHeight),
            CB_FIELD(SwashConstants, waveGroupPhase),
        };
        CB_VERIFY_LAYOUT(SwashConstants, kSwashConstantsFields);
        CB_BIND_HLSL(SwashConstants, kSwashConstantsFields, "WaterShoreSwashConstants");

        /// @brief 岸の泡のパスの計算シェーダー
        struct FoamShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreFoam.CS.hlsl"; }
        };

        /// @brief 海底の高さをならすパスの計算シェーダー
        struct SmoothSeabedShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreSeabedSmooth.CS.hlsl"; }
        };

        /// @brief 寄せ・引きのずれのパスの計算シェーダー
        struct SwashShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreSwash.CS.hlsl"; }
        };

        bool CreatePipelines();
        bool CreateResources();

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        bool isInitialized_ = false;
        Settings settings_{};

        // ---- 岸の泡（カメラの周りの範囲。水の粒の静止位置ごと）----
        CustomShaderPipeline foamPipeline_{};
        FoamShaderProvider foamShaderProvider_{};
        /// @brief 書き込み先は frameIndex_ & 1
        FFTOceanPingPong foam_{};
        uint32_t frameIndex_ = 0;
        bool resetPending_ = true;
        float previousTimeSeconds_ = 0.0f;
        uint32_t spectrumRevision_ = 0;
        float prevWindowOriginXZ_[2] = { 0.0f, 0.0f };
        uint64_t lastFrameNumber_ = UINT64_MAX;

        // ---- ならした海底の高さ（寄せ・引きの勾配と水深を測る）----
        CustomShaderPipeline smoothSeabedPipeline_{};
        SmoothSeabedShaderProvider smoothSeabedShaderProvider_{};
        FFTOceanGpuTexture smoothSeabed_{};

        // ---- 寄せ・引きのずれ（ならした海底と同じ粗い格子）----
        CustomShaderPipeline swashPipeline_{};
        SwashShaderProvider swashShaderProvider_{};
        FFTOceanGpuTexture swash_{};
    };
}

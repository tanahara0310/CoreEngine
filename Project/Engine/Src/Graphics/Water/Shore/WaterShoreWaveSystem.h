#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/FFTOceanGpuResources.h"

#include <array>
#include <cstdint>
#include <d3d12.h>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;

    /// @brief 岸の波の「場」を GPU で作るシステム
    /// @details 海底の高さと同じ範囲（カメラの周り）に、(到着時刻, ∂T/∂x, ∂T/∂z, ならした静水深) の場を作る。
    ///          到着時刻は、沖からうねりの向きに進む平面波が水深で決まる速さで届く時刻で、
    ///          毎フレーム前のフレームの値を引き継いで数回ずつ解き進める。波の形は場と時刻から
    ///          シェーダーが式で求める（Water/Common/WaterShoreWave.hlsli）
    class WaterShoreWaveSystem
    {
    public:
        /// @brief 場の一辺のテクセル数
        static constexpr uint32_t kResolution = 256;
        /// @brief 1 フレームに到着時刻を解き進める回数
        static constexpr uint32_t kSolveIterationsPerFrame = 32;

        /// @brief 岸の波の設定（WaterCVars の r.Water.Shore.*）
        struct Settings {
            bool enabled = true;
            float height = 0.8f;          ///< 沖（水深 startDepth）での波高 [m]
            float period = 8.0f;          ///< 周期 [s]
            float startDepth = 4.0f;      ///< 波が来始める水深 [m]
            float heightVariation = 0.35f;///< 1 本ごとの波高のばらつき
            float direction[2] = { 0.0f, 1.0f }; ///< 沖から来る波の進む向き（XZ。うねりの向き）
        };

        /// @brief 1 フレーム分の入力
        struct Input {
            D3D12_GPU_DESCRIPTOR_HANDLE seabedHeightSRV{}; ///< 海底の高さ（RTWaterSeabedPass）
            float windowOriginXZ[2] = { 0.0f, 0.0f };      ///< 範囲の XZ の最小の角 [m]
            float windowSize = 0.0f;                       ///< 範囲の一辺 [m]
            float waterRestHeight = 0.0f;                  ///< 静水面の高さ [m]
            uint64_t frameNumber = 0;
        };

        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator);
        bool IsInitialized() const { return isInitialized_; }

        void SetSettings(const Settings& settings);
        const Settings& GetSettings() const { return settings_; }

        /// @brief 場を作るか（初期化済みで、設定で有効）
        bool IsActive() const { return isInitialized_ && settings_.enabled; }

        /// @brief 場を 1 フレーム分作り直す（範囲が動いたら前の到着時刻を位置で読み替える）
        void Dispatch(ID3D12GraphicsCommandList* cmdList, const Input& input);

        /// @brief 場を一度でも作ったか
        bool HasField() const { return fieldReady_; }

        /// @brief 場の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetFieldSRVHandle() const { return field_.srv.gpuHandle; }
        /// @brief 場のテクスチャ（ステート追跡つき）
        GpuResource& GetFieldResource() { return field_; }

        /// @brief 角周波数 ω [rad/s]
        float GetAngularFrequency() const;

        /// @brief シェーダーへ渡す波の時刻 [s]
        /// @details 波が届く間隔に揺らぎを足し、周期の整数倍で折り返した値
        float ComputePhaseTime(double timeSeconds) const;

    private:
        /// @brief 水深と到着時刻の初期値のパスの定数（WaterShoreWaveInit.CS.hlsl）
        struct InitConstants {
            float windowOriginXZ[2] = { 0.0f, 0.0f };
            float windowSize = 0.0f;
            uint32_t resolution = kResolution;
            float prevWindowOriginXZ[2] = { 0.0f, 0.0f };
            float waterRestHeight = 0.0f;
            float startDepth = 0.0f;
            uint32_t resetArrival = 1;
            float direction[2] = { 0.0f, 1.0f };
            float angularFrequency = 1.0f;
        };
        static constexpr Cb::Field kInitConstantsFields[] = {
            CB_FIELD(InitConstants, windowOriginXZ), CB_FIELD(InitConstants, windowSize),
            CB_FIELD(InitConstants, resolution), CB_FIELD(InitConstants, prevWindowOriginXZ),
            CB_FIELD(InitConstants, waterRestHeight), CB_FIELD(InitConstants, startDepth),
            CB_FIELD(InitConstants, resetArrival), CB_FIELD(InitConstants, direction),
            CB_FIELD(InitConstants, angularFrequency),
        };
        CB_VERIFY_LAYOUT(InitConstants, kInitConstantsFields);
        CB_BIND_HLSL(InitConstants, kInitConstantsFields, "WaterShoreWaveInitConstants");

        /// @brief 到着時刻を進めるパスの定数（WaterShoreWaveSolve.CS.hlsl）
        struct SolveConstants {
            uint32_t resolution = kResolution;
            float texelSize = 1.0f;
            float angularFrequency = 1.0f;
            float startDepth = 0.0f;
            float windowOriginXZ[2] = { 0.0f, 0.0f };
            float direction[2] = { 0.0f, 1.0f };
        };
        static constexpr Cb::Field kSolveConstantsFields[] = {
            CB_FIELD(SolveConstants, resolution), CB_FIELD(SolveConstants, texelSize),
            CB_FIELD(SolveConstants, angularFrequency), CB_FIELD(SolveConstants, startDepth),
            CB_FIELD(SolveConstants, windowOriginXZ), CB_FIELD(SolveConstants, direction),
        };
        CB_VERIFY_LAYOUT(SolveConstants, kSolveConstantsFields);
        CB_BIND_HLSL(SolveConstants, kSolveConstantsFields, "WaterShoreWaveSolveConstants");

        /// @brief 場にまとめるパスの定数（WaterShoreWaveFinalize.CS.hlsl）
        struct FinalizeConstants {
            uint32_t resolution = kResolution;
            float texelSize = 1.0f;
            float padding[2] = { 0.0f, 0.0f };
        };
        static constexpr Cb::Field kFinalizeConstantsFields[] = {
            CB_FIELD(FinalizeConstants, resolution), CB_FIELD(FinalizeConstants, texelSize),
            CB_FIELD(FinalizeConstants, padding),
        };
        CB_VERIFY_LAYOUT(FinalizeConstants, kFinalizeConstantsFields);
        CB_BIND_HLSL(FinalizeConstants, kFinalizeConstantsFields, "WaterShoreWaveFinalizeConstants");

        struct InitShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreWaveInit.CS.hlsl"; }
        };
        struct SolveShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreWaveSolve.CS.hlsl"; }
        };
        struct FinalizeShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterShoreWaveFinalize.CS.hlsl"; }
        };

        bool CreatePipelines();
        bool CreateResources();
        bool CreateTexture(FFTOceanGpuTexture& texture, DXGI_FORMAT format, const char* name);

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        bool isInitialized_ = false;
        Settings settings_{};

        CustomShaderPipeline initPipeline_{};
        CustomShaderPipeline solvePipeline_{};
        CustomShaderPipeline finalizePipeline_{};
        InitShaderProvider initShaderProvider_{};
        SolveShaderProvider solveShaderProvider_{};
        FinalizeShaderProvider finalizeShaderProvider_{};

        FFTOceanGpuTexture depth_{};
        /// @brief 到着時刻（前のフレームの最後に書いた方が arrivalLatest_）
        FFTOceanPingPong arrival_{};
        uint32_t arrivalLatest_ = 0;
        FFTOceanGpuTexture field_{};

        bool resetPending_ = true;
        bool fieldReady_ = false;
        float prevWindowOriginXZ_[2] = { 0.0f, 0.0f };
        uint64_t lastFrameNumber_ = UINT64_MAX;
    };
}

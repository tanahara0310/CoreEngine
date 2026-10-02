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
    /// @details 白波の泡は FFT の参照格子（カスケードごとの周期テクスチャ）の上で蓄積・減衰させる
    class WaterFoamSystem
    {
    public:
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

        /// @brief 白波の蓄積パスの計算シェーダー
        struct WhitecapShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanFoamAccumulate.CS.hlsl"; }
        };

        bool CreatePipelines();
        bool CreateWhitecapResources();

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
    };
}

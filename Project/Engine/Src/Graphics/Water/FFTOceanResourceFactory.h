#pragma once

#include "Graphics/Water/FFTOceanGpuResources.h"

#include <cstdint>
#include <wrl.h>

namespace CoreEngine
{
    class DescriptorManager;

    /// @brief FFT Ocean のGPUリソース生成を担当するファクトリ
    class FFTOceanResourceFactory {
    public:
        /// @brief IFFT ピンポン用中間テクスチャ（A/B × 2枚）を作成する
        static bool CreateIntermediateTextures(
            ID3D12Device* device,
            DescriptorManager* descriptorManager,
            uint32_t resolution,
            FFTOceanPingPong& spectrumA,
            FFTOceanPingPong& spectrumB);

        /// @brief カスケード 1 本分の初期スペクトルバッファ（DEFAULT + UPLOAD）を作成する
        /// @details SRV は DEFAULT 側に作る。CPU 書き込みは outSet.mapped（UPLOAD 側）へ。
        static bool CreateSpectrumBuffers(
            ID3D12Device* device,
            DescriptorManager* descriptorManager,
            uint32_t resolution,
            uint32_t sampleStride,
            FFTOceanSpectrumBufferSet& outSet);

        static bool CreateSimulationConstantBuffer(
            ID3D12Device* device,
            uint32_t constantSize,
            Microsoft::WRL::ComPtr<ID3D12Resource>& simulationConstantsBuffer,
            void*& mappedSimulationConstants);

        static bool CreateIFFTConstantBuffer(
            ID3D12Device* device,
            uint32_t constantSize,
            uint32_t maxPassCount,
            Microsoft::WRL::ComPtr<ID3D12Resource>& ifftConstantsBuffer,
            uint8_t*& mappedIFFTConstantsData);
    };
}

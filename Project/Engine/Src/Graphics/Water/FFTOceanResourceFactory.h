#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class DescriptorManager;

    /// @brief FFT Ocean のGPUリソース生成を担当するファクトリ
    class FFTOceanResourceFactory {
    public:
        static bool CreateIntermediateTextures(
            ID3D12Device* device,
            DescriptorManager* descriptorManager,
            uint32_t resolution,
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureA,
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureB,
            std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumASrvCpuHandle,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumASrvHandle,
            std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumAUavCpuHandle,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumAUavHandle,
            std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumBSrvCpuHandle,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumBSrvHandle,
            std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumBUavCpuHandle,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumBUavHandle,
            std::array<D3D12_RESOURCE_STATES, 2>& spectrumAState,
            std::array<D3D12_RESOURCE_STATES, 2>& spectrumBState);

        static bool CreateSpectrumBuffers(
            ID3D12Device* device,
            DescriptorManager* descriptorManager,
            uint32_t resolution,
            uint32_t sampleStride,
            Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumBuffer,
            Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumUploadBuffer,
            void*& mappedSpectrumSamples,
            D3D12_CPU_DESCRIPTOR_HANDLE& spectrumSrvCpuHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE& spectrumSrvHandle,
            D3D12_RESOURCE_STATES& spectrumBufferState,
            bool& spectrumBufferDirty);

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

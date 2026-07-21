#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"

#include <array>
#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT Ocean のGPUディスパッチ実処理を分離するヘルパー
    class FFTOceanDispatchHelper {
    public:
        /// @brief 時間発展パスを実行する
        static void DispatchEvolutionPass(
            ID3D12GraphicsCommandList* cmdList,
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureA,
            std::array<D3D12_RESOURCE_STATES, 2>& spectrumAState,
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureB,
            std::array<D3D12_RESOURCE_STATES, 2>& spectrumBState,
            CustomShaderPipeline& evolutionPipeline,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumAUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumBUavHandle,
            D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
            uint32_t resolution);

        /// @brief IFFT単一ステージを実行する
        static void DispatchIFFTPass(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* inputResource,
            D3D12_RESOURCE_STATES& inputState,
            CustomShaderPipeline& pipeline,
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
            ID3D12Resource* outputResource,
            D3D12_RESOURCE_STATES& outputState,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUav,
            D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress,
            uint32_t resolution);

        /// @brief 最終出力パスを実行する
        static void DispatchFinalizePass(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* spectrumAResource,
            D3D12_RESOURCE_STATES& spectrumAState,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumASrv,
            ID3D12Resource* spectrumBResource,
            D3D12_RESOURCE_STATES& spectrumBState,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumBSrv,
            CustomShaderPipeline& finalizePipeline,
            D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianUavHandle,
            D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
            uint32_t resolution);
    };
}

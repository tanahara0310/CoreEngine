#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Water/FFTOceanGpuResources.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT Ocean のGPUディスパッチ実処理を分離するヘルパー
    /// @details リソースは FFTOceanGpuTexture（本体＋ステート＋ビューの束）で受け取る。
    class FFTOceanDispatchHelper {
    public:
        /// @brief 時間発展パスを実行する
        /// @details 書き込み先は ping-pong の index 0（A/B とも）。index 1 には触れない。
        static void DispatchEvolutionPass(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanPingPong& spectrumA,
            FFTOceanPingPong& spectrumB,
            CustomShaderPipeline& evolutionPipeline,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumSrvHandle,
            D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
            uint32_t resolution);

        /// @brief IFFT単一ステージを実行する（input を SRV、output を UAV へ遷移して1回 Dispatch）
        static void DispatchIFFTPass(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanGpuTexture& input,
            FFTOceanGpuTexture& output,
            CustomShaderPipeline& pipeline,
            D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress,
            uint32_t resolution);

        /// @brief 最終出力パスを実行する（出力先スライス UAV は呼び出し側がカスケードから選ぶ）
        static void DispatchFinalizePass(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanGpuTexture& spectrumA,
            FFTOceanGpuTexture& spectrumB,
            CustomShaderPipeline& finalizePipeline,
            D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianUavHandle,
            D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
            uint32_t resolution);
    };
}

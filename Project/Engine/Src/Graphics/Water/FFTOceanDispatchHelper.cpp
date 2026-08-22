#include "pch.h"
#include "FFTOceanDispatchHelper.h"

#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"

namespace CoreEngine
{
    void FFTOceanDispatchHelper::DispatchEvolutionPass(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanPingPong& spectrumA,
        FFTOceanPingPong& spectrumB,
        CustomShaderPipeline& evolutionPipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumSrvHandle,
        D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
        uint32_t resolution)
    {
        // 時間発展が書くのは ping-pong の index 0 のみ。index 1 は後続の IFFT が
        // 追跡ステートから必要な状態へ遷移させるため、ここでは触らない
        // （以前は index 1 を毎回 COMMON へ落としており、カスケード×フレームごとに
        //   無意味なバリアを発行していた）。
        ResourceBarrierHelper::Transition(
            cmdList, spectrumA[0].Get(), spectrumA[0].state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(
            cmdList, spectrumB[0].Get(), spectrumB[0].state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(evolutionPipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(evolutionPipeline.GetComputeRootSignature());

        const int spectrumSlot = evolutionPipeline.GetComputeRootParamIndex("gSpectrumSamples");
        if (spectrumSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumSlot), spectrumSrvHandle);
        }

        const int spectrumAOutputSlot = evolutionPipeline.GetComputeRootParamIndex("gHeightDisplacementXOutput");
        if (spectrumAOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumAOutputSlot), spectrumA[0].uav);
        }

        const int spectrumBOutputSlot = evolutionPipeline.GetComputeRootParamIndex("gDisplacementZOutput");
        if (spectrumBOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumBOutputSlot), spectrumB[0].uav);
        }

        const int constantsSlot = evolutionPipeline.GetComputeRootParamIndex("FFTOceanSimulationConstants");
        if (constantsSlot >= 0 && simulationConstantsAddress != 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                simulationConstantsAddress);
        }

        const UINT dispatchX = (resolution + 7) / 8;
        const UINT dispatchY = (resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, 1);

        ResourceBarrierHelper::UAV(cmdList, spectrumA[0].Get());
        ResourceBarrierHelper::UAV(cmdList, spectrumB[0].Get());
    }

    void FFTOceanDispatchHelper::DispatchIFFTPass(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanGpuTexture& input,
        FFTOceanGpuTexture& output,
        CustomShaderPipeline& pipeline,
        D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress,
        uint32_t resolution)
    {
        ResourceBarrierHelper::Transition(cmdList, input.Get(), input.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, output.Get(), output.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(pipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());

        const int inputSlot = pipeline.GetComputeRootParamIndex("gInputSpectrum");
        if (inputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(inputSlot), input.srv);
        }

        const int outputSlot = pipeline.GetComputeRootParamIndex("gOutputSpectrum");
        if (outputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outputSlot), output.uav);
        }

        const int constantsSlot = pipeline.GetComputeRootParamIndex("FFTOceanIFFTConstants");
        if (constantsSlot >= 0 && ifftConstantsGpuAddress != 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                ifftConstantsGpuAddress);
        }

        const UINT dispatchX = (resolution + 7) / 8;
        const UINT dispatchY = (resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, 1);
        ResourceBarrierHelper::UAV(cmdList, output.Get());
    }

    void FFTOceanDispatchHelper::DispatchFinalizePass(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanGpuTexture& spectrumA,
        FFTOceanGpuTexture& spectrumB,
        CustomShaderPipeline& finalizePipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianUavHandle,
        D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
        uint32_t resolution)
    {
        ResourceBarrierHelper::Transition(cmdList, spectrumA.Get(), spectrumA.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumB.Get(), spectrumB.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(finalizePipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(finalizePipeline.GetComputeRootSignature());

        const int spectrumASlot = finalizePipeline.GetComputeRootParamIndex("gHeightDisplacementXSpectrum");
        if (spectrumASlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumASlot), spectrumA.srv);
        }

        const int spectrumBSlot = finalizePipeline.GetComputeRootParamIndex("gDisplacementZSpectrum");
        if (spectrumBSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumBSlot), spectrumB.srv);
        }

        const int displacementOutputSlot = finalizePipeline.GetComputeRootParamIndex("gDisplacementOutput");
        if (displacementOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(displacementOutputSlot), displacementUavHandle);
        }

        const int normalOutputSlot = finalizePipeline.GetComputeRootParamIndex("gNormalOutput");
        if (normalOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(normalOutputSlot), normalUavHandle);
        }

        const int jacobianOutputSlot = finalizePipeline.GetComputeRootParamIndex("gJacobianOutput");
        if (jacobianOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(jacobianOutputSlot), jacobianUavHandle);
        }

        const int constantsSlot = finalizePipeline.GetComputeRootParamIndex("FFTOceanSimulationConstants");
        if (constantsSlot >= 0 && simulationConstantsAddress != 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                simulationConstantsAddress);
        }

        const UINT dispatchX = (resolution + 7) / 8;
        const UINT dispatchY = (resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, 1);
    }
}
